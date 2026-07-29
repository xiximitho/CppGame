#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/log.hpp"
#include "core/time.hpp"
#include "net/protocol.hpp"
#include "net/transport.hpp"
#include "sim/components.hpp"
#include "sim/map_gen.hpp"
#include "sim/rng.hpp"
#include "sim/systems.hpp"
#include "sim/world.hpp"

// The authoritative server. Links no SDL, no graphics, no audio: it builds and
// runs on a machine that has none of them installed (see the `server-only`
// CMake preset).

namespace {

/// Snapshots are sent every third tick — 10 Hz at a 30 Hz simulation. Movement
/// stays smooth regardless because a snapshot describes a step's progress rather
/// than an instantaneous position, so the client interpolates between updates
/// instead of waiting for them.
constexpr sim::Tick kSnapshotEveryTicks = 3;

/// Cap on map chunks pushed to one player per tick, so a fresh join trickles the
/// world in instead of bursting a megabyte into a reliable queue.
constexpr int kMaxChunksPerTick = 6;

struct Options {
    std::uint16_t port = net::kDefaultPort;
    std::uint64_t seed = 1337;
    int           wanderers = 60;
    std::size_t   max_peers = 64;
};

/// Per-connection state. Deliberately outside sim/: which chunks a socket has
/// already been sent is a networking concern, and the simulation must not know
/// that sockets exist.
struct Connection {
    net::PeerId peer = net::kInvalidPeer;
    sim::NetId  net_id = sim::kInvalidNetId;
    std::string name;
    bool        welcomed = false;
    std::unordered_set<std::uint64_t> sent_chunks;
};

std::uint64_t chunk_key(int chunk_x, int chunk_y, int z) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) << 40U) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunk_y)) << 20U) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunk_x));
}

/// Rounds down to a chunk boundary. Uses floor semantics rather than truncation
/// so it stays correct if the world ever has negative coordinates.
int chunk_origin(int coordinate) {
    const int size = net::kChunkSize;
    const int remainder = ((coordinate % size) + size) % size;
    return coordinate - remainder;
}

void print_usage() {
    std::printf(
        "usage: game_server [options]\n"
        "\n"
        "  --port N         UDP port to listen on (default %u)\n"
        "  --seed N         world seed (default 1337)\n"
        "  --wanderers N    wandering actors to spawn (default 60)\n"
        "  --max-peers N    connection limit (default 64)\n"
        "  --help           this text\n",
        static_cast<unsigned>(net::kDefaultPort));
}

bool parse_args(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = (i + 1) < argc;

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return false;
        }
        if (arg == "--port" && has_value) {
            options.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--seed" && has_value) {
            options.seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--wanderers" && has_value) {
            options.wanderers = std::atoi(argv[++i]);
        } else if (arg == "--max-peers" && has_value) {
            options.max_peers = static_cast<std::size_t>(std::atoi(argv[++i]));
        } else {
            LOG_WARN("ignoring unknown argument '%s'", arg.c_str());
        }
    }
    return true;
}

void send_message(net::ITransport& transport, net::PeerId peer,
                  const net::BitWriter& writer, const std::uint8_t* buffer,
                  net::Channel channel) {
    if (writer.overflowed()) {
        LOG_ERROR("outgoing packet overflowed, dropping");
        return;
    }
    transport.send(peer, buffer, writer.bytes_written(), channel);
}

void send_reject(net::ITransport& transport, net::PeerId peer,
                 const std::string& reason) {
    std::uint8_t buffer[128];
    net::BitWriter writer(buffer, sizeof(buffer));
    net::write_reject(writer, net::RejectMsg{reason});
    send_message(transport, peer, writer, buffer, net::Channel::Reliable);
    transport.flush();
    transport.disconnect(peer);
}

/// Pushes the chunks around a player that they have not already received.
void stream_chunks(const sim::World& world, net::ITransport& transport,
                   Connection& connection, sim::TilePos center) {
    const sim::TileMap& map = world.map();

    // A margin beyond the area of interest means the map is already present when
    // an actor walks into view, rather than arriving a moment late.
    constexpr int kMargin = 4;
    const int min_x = chunk_origin(center.x - sim::kAoiHalfX - kMargin);
    const int max_x = chunk_origin(center.x + sim::kAoiHalfX + kMargin);
    const int min_y = chunk_origin(center.y - sim::kAoiHalfY - kMargin);
    const int max_y = chunk_origin(center.y + sim::kAoiHalfY + kMargin);

    int sent = 0;
    for (int z = 0; z < map.floors() && sent < kMaxChunksPerTick; ++z) {
        for (int cy = min_y; cy <= max_y && sent < kMaxChunksPerTick;
             cy += net::kChunkSize) {
            for (int cx = min_x; cx <= max_x && sent < kMaxChunksPerTick;
                 cx += net::kChunkSize) {
                const std::uint64_t key = chunk_key(cx, cy, z);
                if (connection.sent_chunks.count(key) != 0) {
                    continue;
                }

                net::MapChunkMsg chunk;
                chunk.chunk_x = static_cast<std::int16_t>(cx);
                chunk.chunk_y = static_cast<std::int16_t>(cy);
                chunk.z = static_cast<std::int8_t>(z);
                chunk.tiles.reserve(net::kChunkTileCount);

                bool any_content = false;
                for (int row = 0; row < net::kChunkSize; ++row) {
                    for (int col = 0; col < net::kChunkSize; ++col) {
                        const sim::TilePos pos{
                            static_cast<std::int16_t>(cx + col),
                            static_cast<std::int16_t>(cy + row),
                            static_cast<std::int8_t>(z)};
                        const sim::Tile tile = map.at(pos);
                        if (tile.ground != sim::kTileEmpty) {
                            any_content = true;
                        }
                        chunk.tiles.push_back(tile);
                    }
                }

                // Entirely empty chunks are marked sent without transmitting:
                // the client already treats unknown tiles as empty. On a world
                // with sparse upper floors this removes most of the traffic.
                connection.sent_chunks.insert(key);
                if (!any_content) {
                    continue;
                }

                std::uint8_t buffer[net::kMaxPacketBytes];
                net::BitWriter writer(buffer, sizeof(buffer));
                net::write_map_chunk(writer, chunk);
                send_message(transport, connection.peer, writer, buffer,
                             net::Channel::Reliable);
                ++sent;
            }
        }
    }
}

void handle_hello(sim::World& world, sim::Rng& rng, net::ITransport& transport,
                  Connection& connection, net::BitReader& reader) {
    net::HelloMsg hello;
    if (!net::read_hello(reader, hello)) {
        send_reject(transport, connection.peer, "malformed hello");
        return;
    }
    if (hello.protocol != net::kProtocolVersion) {
        LOG_WARN("peer %u speaks protocol %u, we speak %u", connection.peer,
                 hello.protocol, net::kProtocolVersion);
        send_reject(transport, connection.peer, "protocol version mismatch");
        return;
    }
    if (connection.welcomed) {
        LOG_WARN("peer %u sent a second hello, ignoring", connection.peer);
        return;
    }

    connection.name = hello.name.empty() ? "anonymous" : hello.name;

    const sim::TilePos spawn = sim::find_spawn_tile(world.map(), rng);
    connection.net_id = world.allocate_net_id();
    world.spawn_actor(connection.net_id, spawn, 0);
    connection.welcomed = true;

    net::WelcomeMsg welcome;
    welcome.your_id = connection.net_id;
    welcome.tick = world.tick();
    welcome.map_width = static_cast<std::uint16_t>(world.map().width());
    welcome.map_height = static_cast<std::uint16_t>(world.map().height());
    welcome.map_floors = static_cast<std::uint8_t>(world.map().floors());
    welcome.spawn = spawn;

    std::uint8_t buffer[128];
    net::BitWriter writer(buffer, sizeof(buffer));
    net::write_welcome(writer, welcome);
    send_message(transport, connection.peer, writer, buffer,
                 net::Channel::Reliable);

    LOG_INFO("'%s' joined as actor %u at (%d,%d,%d)", connection.name.c_str(),
             connection.net_id, spawn.x, spawn.y, static_cast<int>(spawn.z));
}

}  // namespace

int main(int argc, char** argv) {
    core::log_set_tag("server");

    Options options;
    if (!parse_args(argc, argv, options)) {
        return 0;
    }

    sim::World world(
        sim::generate_demo_map(sim::MapGenSettings{96, 96, 3, options.seed}));
    sim::Rng rng(options.seed ^ 0xA24BAED4963EE407ULL);

    for (int i = 0; i < options.wanderers; ++i) {
        const sim::TilePos at = sim::find_spawn_tile(world.map(), rng);
        if (world.occupant(at) != sim::kInvalidNetId) {
            continue;
        }
        const sim::NetId id = world.allocate_net_id();
        const entt::entity entity = world.spawn_actor(id, at, 0);
        world.registry().emplace<sim::CWanderer>(entity, sim::CWanderer{0});
    }

    auto transport = net::create_server(options.port, options.max_peers);
    if (transport == nullptr) {
        return 1;
    }

    std::unordered_map<net::PeerId, Connection> connections;

    constexpr std::uint64_t kTickNanos = 1'000'000'000ULL / sim::kSimHz;
    std::uint64_t last_time = core::now_nanos();
    std::uint64_t accumulator = 0;

    LOG_INFO("world %dx%dx%d, %d wanderers, simulating at %d Hz",
             world.map().width(), world.map().height(), world.map().floors(),
             options.wanderers, sim::kSimHz);

    for (;;) {
        // Network first: input that arrived since the last tick should be acted
        // on by this tick, not the next one.
        net::Event event;
        while (transport->poll(event)) {
            switch (event.type) {
                case net::EventType::Connected: {
                    Connection connection;
                    connection.peer = event.peer;
                    connections[event.peer] = std::move(connection);
                    LOG_INFO("peer %u connected (%zu total)", event.peer,
                             transport->peer_count());
                    break;
                }

                case net::EventType::Disconnected: {
                    const auto it = connections.find(event.peer);
                    if (it != connections.end()) {
                        if (it->second.net_id != sim::kInvalidNetId) {
                            world.despawn(it->second.net_id);
                        }
                        LOG_INFO("'%s' left", it->second.name.c_str());
                        connections.erase(it);
                    }
                    break;
                }

                case net::EventType::Data: {
                    const auto it = connections.find(event.peer);
                    if (it == connections.end()) {
                        break;
                    }
                    Connection& connection = it->second;

                    net::BitReader reader(event.data.data(), event.data.size());
                    switch (net::read_msg_id(reader)) {
                        case net::MsgId::C2S_Hello:
                            handle_hello(world, rng, *transport, connection,
                                         reader);
                            break;

                        case net::MsgId::C2S_Input: {
                            net::InputMsg input;
                            if (!net::read_input(reader, input) ||
                                !connection.welcomed) {
                                break;
                            }
                            // Manual input takes back control from auto-walking.
                            world.cancel_path(connection.net_id);
                            if (input.walk) {
                                world.request_walk(connection.net_id, input.dir);
                            } else {
                                world.request_turn(connection.net_id, input.dir);
                            }
                            break;
                        }

                        case net::MsgId::C2S_MoveTo: {
                            net::MoveToMsg move;
                            if (!net::read_move_to(reader, move) ||
                                !connection.welcomed) {
                                break;
                            }
                            // The target came off the wire, so it is untrusted:
                            // request_move_to rejects out-of-bounds and unwalkable
                            // tiles rather than trusting the client's aim.
                            if (!world.request_move_to(connection.net_id,
                                                       move.target)) {
                                LOG_DEBUG("no route for '%s' to (%d,%d,%d)",
                                          connection.name.c_str(), move.target.x,
                                          move.target.y,
                                          static_cast<int>(move.target.z));
                            }
                            break;
                        }

                        default:
                            // Server-to-client ids, or garbage. A well-behaved
                            // client never sends these; ignoring is correct and
                            // costs nothing.
                            LOG_DEBUG("unexpected message from peer %u",
                                      event.peer);
                            break;
                    }
                    break;
                }

                case net::EventType::None:
                    break;
            }
        }

        const std::uint64_t now = core::now_nanos();
        std::uint64_t elapsed = now - last_time;
        last_time = now;

        // Never replay a long stall as a burst of catch-up ticks: that would
        // teleport every actor and desynchronise clients that are interpolating.
        constexpr std::uint64_t kMaxElapsed = 500'000'000ULL;
        if (elapsed > kMaxElapsed) {
            LOG_WARN("dropping %llu ms of simulation time",
                     static_cast<unsigned long long>((elapsed - kMaxElapsed) /
                                                     1'000'000ULL));
            elapsed = kMaxElapsed;
        }
        accumulator += elapsed;

        while (accumulator >= kTickNanos) {
            accumulator -= kTickNanos;

            world.step();
            sim::update_path_followers(world);
            sim::update_wanderers(world, rng);

            if (world.tick() % kSnapshotEveryTicks != 0) {
                continue;
            }

            for (auto& [peer, connection] : connections) {
                if (!connection.welcomed) {
                    continue;
                }
                const entt::entity entity = world.lookup(connection.net_id);
                if (entity == entt::null) {
                    continue;
                }
                const sim::TilePos center =
                    world.registry().get<sim::CPosition>(entity).tile;

                stream_chunks(world, *transport, connection, center);

                sim::Snapshot snapshot;
                sim::build_snapshot(world, center, snapshot);

                std::uint8_t buffer[net::kMaxPacketBytes];
                net::BitWriter writer(buffer, sizeof(buffer));
                net::write_snapshot(writer, snapshot);
                send_message(*transport, peer, writer, buffer,
                             net::Channel::Unreliable);
            }
        }

        transport->flush();

        // Yield the core. Without this the loop spins at 100% between ticks.
        core::sleep_nanos(1'000'000ULL);
    }
}

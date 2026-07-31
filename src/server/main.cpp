#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/log.hpp"
#include "core/time.hpp"
#include "net/protocol.hpp"
#include "net/transport.hpp"
#include "sim/components.hpp"
#include "sim/content_blob.hpp"
#include "sim/item_type.hpp"
#include "sim/map_gen.hpp"
#include "sim/map_io.hpp"
#include "sim/monster_io.hpp"
#include "sim/rng.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "store/content.hpp"
#include "store/db.hpp"
#include "store/players.hpp"
#include "sim/world.hpp"

namespace {

/// Set by SIGINT/SIGTERM so the loop can exit and save.
///
/// A signal handler may do almost nothing safely; writing a volatile sig_atomic_t is
/// one of the few things it may. Everything else — the final save, the logging —
/// happens back on the main path where it is allowed to.
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_stop_signal(int) { g_stop = 1; }

/// Reads a whole text file. The server links no SDL (server-only preset), so it
/// cannot use platform::vfs; plain <fstream> is fine because a server never runs
/// on Android. Returns an empty string when the file cannot be opened.
std::string read_text_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// Loads the monster catalogue, falling back to the built-in classes.
///
/// Read with <fstream> for the same reason the map is: the server links no SDL, so
/// it has no VFS, and it never runs on Android. A malformed file is loud and then
/// ignored — a server that refuses to boot because someone fat-fingered a stat is
/// worse than one that boots with the shipped numbers and says so.
sim::MonsterRegistry load_monsters(const std::string& path) {
    const std::string text = read_text_file(path);
    if (text.empty()) {
        LOG_INFO("no monster catalogue '%s'; using built-in classes",
                 path.c_str());
        return sim::default_monsters();
    }
    sim::MonsterRegistry loaded;
    std::string error;
    if (!sim::parse_monster_catalogue(text, loaded, &error)) {
        LOG_WARN("monster catalogue '%s' failed to parse: %s; using built-in "
                 "classes", path.c_str(), error.c_str());
        return sim::default_monsters();
    }
    LOG_INFO("loaded %zu monster class(es) from '%s'", loaded.count(),
             path.c_str());
    return loaded;
}

/// A built world plus what the map file said about it: the authored player spawn,
/// the one-off monsters and the spawn points, carried out of the single parse.
struct ServerWorld {
    sim::World                     world;
    std::optional<sim::TilePos>    spawn;
    std::vector<sim::MonsterSpawn> monsters;
    std::vector<sim::SpawnerSpec>  spawners;
};

/// Builds the authoritative World. Same map format, blocking derivation and
/// monster spawning as the client, so solo and multiplayer never diverge; only how
/// the bytes are read differs by layer.
ServerWorld build_server_world(const std::string& map_path, std::uint64_t seed,
                               const sim::ItemTypeRegistry& item_types,
                               const sim::MonsterRegistry& monsters) {
    const std::string text = read_text_file(map_path);
    if (!text.empty()) {
        std::string error;
        if (auto parsed = sim::parse_text_map(text, item_types, &error)) {
            LOG_INFO("loaded map '%s' (%dx%d), %zu monster(s) + %zu spawner(s)",
                     map_path.c_str(), parsed->map.width(), parsed->map.height(),
                     parsed->monsters.size(), parsed->spawners.size());
            return ServerWorld{
                sim::World(std::move(parsed->map), item_types, monsters),
                parsed->spawn, std::move(parsed->monsters),
                std::move(parsed->spawners)};
        }
        LOG_WARN("map '%s' failed to parse: %s; using generated map",
                 map_path.c_str(), error.c_str());
    } else {
        LOG_INFO("no map '%s'; using generated map", map_path.c_str());
    }
    return ServerWorld{
        sim::World(sim::generate_demo_map(sim::MapGenSettings{96, 96, 3, seed},
                                          item_types),
                   item_types, monsters),
        std::nullopt,
        {},
        {}};
}

}  // namespace

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
    /// Extra mobs of random classes scattered over the map, on top of whatever the
    /// map itself authors. Zero by default now that maps carry their own monsters
    /// and spawn points: a world that is half authored and half random reads as
    /// random.
    int           wanderers = 0;
    std::size_t   max_peers = 64;
    /// Relative to the working directory: the server reads plain files (it links
    /// no SDL, so no VFS) and never runs on Android.
    std::string   map_path = "assets/maps/dungeon.txt";
    /// Monster classes. Same convention as the map path.
    std::string   monsters_path = "assets/monsters.txt";
    /// Relative to the working directory, same convention as the map path.
    std::string   content_path = "assets/content.db";
    /// Separate file from content on purpose: content is committed, saves are not.
    std::string   players_path = "players.db";
    /// How often connected players are written out, in seconds. A crash loses at
    /// most this much progress; zero disables periodic saving.
    int           save_every_seconds = 60;
};

/// Per-connection state. Deliberately outside sim/: which chunks a socket has
/// already been sent is a networking concern, and the simulation must not know
/// that sockets exist.
struct Connection {
    net::PeerId peer = net::kInvalidPeer;
    sim::NetId  net_id = sim::kInvalidNetId;
    std::string name;
    bool        welcomed = false;
    /// rowid of the stored character; 0 until it has been saved once.
    std::int64_t character_id = 0;
    std::unordered_set<std::uint64_t> sent_chunks;
};

/// Reads a connected player's state out of the world, ready to be stored.
///
/// Returns nullopt when the actor is gone (despawned, or never welcomed), because
/// writing a default-constructed character over a real save would be worse than not
/// saving at all.
std::optional<store::CharacterSave> snapshot_character(const sim::World& world,
                                                       const Connection& connection) {
    if (!connection.welcomed || connection.net_id == sim::kInvalidNetId) {
        return std::nullopt;
    }
    const entt::entity entity = world.lookup(connection.net_id);
    if (entity == entt::null) {
        return std::nullopt;
    }
    const entt::registry& registry = world.registry();
    if (!registry.all_of<sim::CPosition>(entity)) {
        return std::nullopt;
    }

    store::CharacterSave save;
    save.id = connection.character_id;
    save.name = connection.name;

    const auto& position = registry.get<sim::CPosition>(entity);
    save.tile = position.tile;
    save.facing = position.facing;

    if (const auto* health = registry.try_get<sim::CHealth>(entity)) {
        save.hp = health->hp;
        save.max_hp = health->max_hp;
    }
    if (const auto* equipment = registry.try_get<sim::CEquipment>(entity)) {
        save.equipment = equipment->slots;
    }
    if (const auto* inventory = registry.try_get<sim::CInventory>(entity)) {
        save.inventory = inventory->items;
    }
    return save;
}

/// Writes one connected player out. Quiet on success, loud on failure: a save that
/// silently does nothing is the worst outcome here.
void persist(store::Db* players, const sim::World& world,
             const Connection& connection) {
    if (players == nullptr) {
        return;
    }
    const std::optional<store::CharacterSave> save =
        snapshot_character(world, connection);
    if (!save.has_value()) {
        return;
    }
    if (!store::save_character(*players, *save)) {
        LOG_ERROR("could not save '%s'", connection.name.c_str());
    }
}

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
        "  --wanderers N    extra random mobs on top of the map's own (default 0)\n"
        "  --map PATH       authored map (default assets/maps/dungeon.txt);\n"
        "                   falls back to the seeded generated map if unreadable\n"
        "  --monsters PATH  monster classes (default assets/monsters.txt);\n"
        "                   falls back to the built-in classes if unreadable\n"
        "  --max-peers N    connection limit (default 64)\n"
        "  --content PATH   content database (default assets/content.db,\n"
        "                   created and seeded if absent)\n"
        "  --players PATH   player save database (default players.db)\n"
        "  --save-every N   seconds between periodic saves, 0 to disable\n"
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
        } else if (arg == "--map" && has_value) {
            options.map_path = argv[++i];
        } else if (arg == "--monsters" && has_value) {
            options.monsters_path = argv[++i];
        } else if (arg == "--max-peers" && has_value) {
            options.max_peers = static_cast<std::size_t>(std::atoi(argv[++i]));
        } else if (arg == "--content" && has_value) {
            options.content_path = argv[++i];
        } else if (arg == "--players" && has_value) {
            options.players_path = argv[++i];
        } else if (arg == "--save-every" && has_value) {
            options.save_every_seconds = std::atoi(argv[++i]);
        } else {
            LOG_WARN("ignoring unknown argument '%s'", arg.c_str());
        }
    }
    return true;
}

void send_message(net::ITransport& transport, net::PeerId peer,
                  const core::BitWriter& writer, const std::uint8_t* buffer,
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
    core::BitWriter writer(buffer, sizeof(buffer));
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
                core::BitWriter writer(buffer, sizeof(buffer));
                net::write_map_chunk(writer, chunk);
                send_message(transport, connection.peer, writer, buffer,
                             net::Channel::Reliable);
                ++sent;
            }
        }
    }
}

void handle_hello(sim::World& world, sim::Rng& rng, net::ITransport& transport,
                  Connection& connection, core::BitReader& reader,
                  std::uint64_t content_fingerprint, store::Db* players,
                  const std::optional<sim::TilePos>& authored_spawn) {
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
    // Content, checked exactly like the protocol version and for the same reason.
    // The two sides read content from different places on purpose — this server from
    // its SQLite database, the client from the baked blob — so proving they agree is
    // the only thing standing between "someone forgot to re-bake" and a world where
    // the client and the server disagree about what blocks and how hard a sword hits.
    if (hello.content_hash != content_fingerprint) {
        LOG_WARN("peer %u has content %016llx, we have %016llx", connection.peer,
                 static_cast<unsigned long long>(hello.content_hash),
                 static_cast<unsigned long long>(content_fingerprint));
        send_reject(transport, connection.peer,
                    "content mismatch: re-run game_bake");
        return;
    }

    connection.name = hello.name.empty() ? "anonymous" : hello.name;

    // A returning character comes back where it logged out; a new one gets a random
    // spawn and the starting kit. nullopt is a first login, not a failure.
    const std::optional<store::CharacterSave> saved =
        players != nullptr ? store::load_character(*players, connection.name)
                           : std::nullopt;

    // A map that names its spawn point decides where new players appear; otherwise
    // it is a random walkable tile, which is every generated map.
    //
    // The authored point is a PREFERENCE, and it is checked: assets/maps/dungeon.txt
    // has its '@' on a wall (someone painted over it in the editor), and trusting
    // that would drop every new player inside solid rock.
    sim::TilePos spawn = sim::find_spawn_tile(world.map(), rng);
    if (authored_spawn.has_value() &&
        world.map().is_walkable(*authored_spawn) &&
        world.occupant(*authored_spawn) == sim::kInvalidNetId) {
        spawn = *authored_spawn;
    } else if (authored_spawn.has_value()) {
        LOG_WARN("map spawn (%d,%d,%d) is not usable; spawning at (%d,%d,%d)",
                 authored_spawn->x, authored_spawn->y,
                 static_cast<int>(authored_spawn->z), spawn.x, spawn.y,
                 static_cast<int>(spawn.z));
    }
    if (saved.has_value()) {
        // The saved tile may have become unusable since — the map is authored and
        // gets edited, and someone may already be standing there. Falling back to a
        // fresh spawn beats refusing the login or dropping the player inside a wall.
        const bool usable = world.map().in_bounds(saved->tile) &&
                            !world.map().at(saved->tile).blocking &&
                            world.occupant(saved->tile) == sim::kInvalidNetId;
        if (usable) {
            spawn = saved->tile;
        } else {
            LOG_WARN("'%s' saved at (%d,%d,%d), which is not usable now; "
                     "spawning fresh",
                     connection.name.c_str(), saved->tile.x, saved->tile.y,
                     static_cast<int>(saved->tile.z));
        }
    }

    connection.net_id = world.allocate_net_id();
    const entt::entity entity = world.spawn_actor(connection.net_id, spawn, 0);
    // Players respawn on death instead of vanishing; monsters do not.
    world.registry().emplace<sim::CRespawn>(entity, sim::CRespawn{spawn});

    if (saved.has_value()) {
        connection.character_id = saved->id;

        auto& position = world.registry().get<sim::CPosition>(entity);
        position.facing = saved->facing;

        auto& health = world.registry().get<sim::CHealth>(entity);
        health.max_hp = saved->max_hp > 0 ? saved->max_hp : health.max_hp;
        // Clamped to at least 1: a character saved at the moment of death would
        // otherwise log back in as a corpse and never get to act.
        health.hp = saved->hp > 0 ? saved->hp : health.max_hp;
        if (health.hp > health.max_hp) {
            health.hp = health.max_hp;
        }

        world.registry().emplace<sim::CEquipment>(entity,
                                                 sim::CEquipment{saved->equipment});
        world.registry().emplace<sim::CInventory>(
            entity, sim::CInventory{saved->inventory});

        LOG_INFO("'%s' restored at (%d,%d,%d) with %d/%d hp",
                 connection.name.c_str(), spawn.x, spawn.y,
                 static_cast<int>(spawn.z), health.hp, health.max_hp);
    } else {
        // Starting kit, so a new player's attack/defense are data-driven from turn
        // one. This is the fallback now, not the only path.
        {
            auto& equipment = world.registry().emplace<sim::CEquipment>(entity);
            equipment.slots[static_cast<std::size_t>(sim::EquipSlot::Weapon)] =
                sim::tiles::kSword;
            equipment.slots[static_cast<std::size_t>(sim::EquipSlot::Body)] =
                sim::tiles::kArmor;
        }
        world.registry().emplace<sim::CInventory>(
            entity,
            sim::CInventory{{{sim::tiles::kBow, 1}, {sim::tiles::kShield, 1}}});
    }

    connection.welcomed = true;

    net::WelcomeMsg welcome;
    welcome.your_id = connection.net_id;
    welcome.tick = world.tick();
    welcome.map_width = static_cast<std::uint16_t>(world.map().width());
    welcome.map_height = static_cast<std::uint16_t>(world.map().height());
    welcome.map_floors = static_cast<std::uint8_t>(world.map().floors());
    welcome.spawn = spawn;

    std::uint8_t buffer[128];
    core::BitWriter writer(buffer, sizeof(buffer));
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

    // Content comes from the database, not from compiled-in tables: adding an item
    // is authoring, not a rebuild (docs/content.md). The connection stays open for
    // the process lifetime because player persistence uses the same file.
    std::optional<store::Db> content = store::open_content_db(options.content_path);
    if (!content.has_value()) {
        LOG_ERROR("cannot open content database '%s'",
                  options.content_path.c_str());
        return 1;
    }
    sim::ItemTypeRegistry item_types;
    if (!store::load_item_types(*content, item_types)) {
        LOG_ERROR("cannot load item types from '%s'",
                  options.content_path.c_str());
        return 1;
    }
    // Serialised in memory purely to be hashed: the server never writes a blob, but
    // hashing the SERIALISED form is what makes this digest comparable with a
    // client's, which computes it from the blob it read. Hashing the in-memory
    // structs instead would compare padding bytes.
    const std::uint64_t content_fingerprint = sim::content_hash(item_types);
    LOG_INFO("loaded %zu item types from '%s' (content %016llx)",
             item_types.count(), options.content_path.c_str(),
             static_cast<unsigned long long>(content_fingerprint));

    // Player saves live in their own file, never alongside authored content: one is
    // committed and shared, the other is this server's private state.
    std::optional<store::Db> players = store::open_player_db(options.players_path);
    if (!players.has_value()) {
        LOG_ERROR("cannot open player database '%s'", options.players_path.c_str());
        return 1;
    }
    store::Db* players_db = &*players;
    LOG_INFO("player database '%s' holds %lld characters",
             options.players_path.c_str(),
             static_cast<long long>(store::character_count(*players).value_or(-1)));

    const sim::MonsterRegistry monster_types = load_monsters(options.monsters_path);
    ServerWorld built = build_server_world(options.map_path, options.seed,
                                          item_types, monster_types);
    sim::World& world = built.world;
    const std::optional<sim::TilePos> authored_spawn = built.spawn;
    sim::Rng rng(options.seed ^ 0xA24BAED4963EE407ULL);

    // Authored mobs first, so they hold the tiles their author chose against the
    // random ones that follow.
    const int authored_monsters =
        sim::spawn_authored_monsters(world, built.monsters);
    const int spawners = sim::create_spawners(world, built.spawners);
    LOG_INFO("%d authored monster(s) placed, %d spawner(s) armed",
             authored_monsters, spawners);

    for (int i = 0; i < options.wanderers; ++i) {
        const sim::TilePos at = sim::find_spawn_tile(world.map(), rng);
        if (world.occupant(at) != sim::kInvalidNetId) {
            continue;
        }
        // Class, stats, speed and loot all come from the monster catalogue, so
        // this stays one line and cannot drift from what solo play spawns.
        sim::spawn_random_monster(world, rng, at);
    }

    auto transport = net::create_server(options.port, options.max_peers);
    if (transport == nullptr) {
        return 1;
    }

    std::unordered_map<net::PeerId, Connection> connections;

    std::signal(SIGINT, on_stop_signal);
    std::signal(SIGTERM, on_stop_signal);

    constexpr std::uint64_t kTickNanos = 1'000'000'000ULL / sim::kSimHz;
    std::uint64_t last_time = core::now_nanos();
    std::uint64_t accumulator = 0;
    std::uint64_t last_save = core::now_nanos();

    LOG_INFO("world %dx%dx%d, %d wanderers, simulating at %d Hz",
             world.map().width(), world.map().height(), world.map().floors(),
             options.wanderers, sim::kSimHz);

    while (g_stop == 0) {
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
                        // Saved BEFORE the despawn: after it, the actor is gone and
                        // there is nothing left to read a position out of.
                        persist(players_db, world, it->second);
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

                    core::BitReader reader(event.data.data(), event.data.size());
                    switch (net::read_msg_id(reader)) {
                        case net::MsgId::C2S_Hello:
                            handle_hello(world, rng, *transport, connection,
                                         reader, content_fingerprint,
                                         players_db, authored_spawn);
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
                            // An explicit destination replaces a chase, the same
                            // way pressing a direction key does.
                            world.cancel_path(connection.net_id);
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

                        case net::MsgId::C2S_Attack: {
                            net::AttackMsg attack;
                            if (!net::read_attack(reader, attack) ||
                                !connection.welcomed) {
                                break;
                            }
                            // Untrusted target id; both primitives ignore unknown
                            // ids and self-targeting.
                            //
                            // Attacking implies chasing (Tibia's chase mode): the
                            // client names WHO, never where, and the server closes
                            // the distance and keeps closing it as the target moves.
                            world.set_attack_target(connection.net_id,
                                                    attack.target);
                            world.request_follow(connection.net_id, attack.target);
                            break;
                        }

                        case net::MsgId::C2S_Equip: {
                            net::EquipMsg equip;
                            if (!net::read_equip(reader, equip) ||
                                !connection.welcomed) {
                                break;
                            }
                            world.equip(connection.net_id, equip.item);
                            break;
                        }

                        case net::MsgId::C2S_Unequip: {
                            net::UnequipMsg unequip;
                            if (!net::read_unequip(reader, unequip) ||
                                !connection.welcomed) {
                                break;
                            }
                            if (unequip.slot < sim::kEquipSlotCount) {
                                world.unequip(
                                    connection.net_id,
                                    static_cast<sim::EquipSlot>(unequip.slot));
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

        // Periodic save, outside the tick loop: it is wall-clock work, not
        // simulation work, and a save must not be able to make the world tick more
        // slowly than it should. A crash loses at most save_every_seconds of
        // progress; the alternative of saving on every change would write the
        // database several times a second per player for no benefit.
        if (options.save_every_seconds > 0) {
            // Both operands spelled as uint64_t: an ULL literal is a different type
            // on this platform and -Wsign-conversion rejects the mix.
            const auto seconds =
                static_cast<std::uint64_t>(options.save_every_seconds);
            const std::uint64_t interval = seconds * std::uint64_t{1'000'000'000};
            if (now - last_save >= interval) {
                last_save = now;
                int saved = 0;
                for (const auto& [peer, connection] : connections) {
                    if (connection.welcomed) {
                        persist(players_db, world, connection);
                        ++saved;
                    }
                }
                if (saved > 0) {
                    LOG_DEBUG("periodic save wrote %d character(s)", saved);
                }
            }
        }

        while (accumulator >= kTickNanos) {
            accumulator -= kTickNanos;

            world.step();
            // Monsters decide before the followers step, so a route chosen this
            // tick is walked on this tick instead of the next one.
            sim::update_spawners(world, rng);
            sim::update_monsters(world, rng);
            sim::update_chasers(world);
            sim::update_path_followers(world);
            sim::update_combat(world);

            if (world.tick() % kSnapshotEveryTicks != 0) {
                continue;
            }

            // Attack effects since the last snapshot. Drained once; each player
            // gets the ones inside its area of interest.
            const std::vector<sim::AttackEvent> effects = world.attack_events();
            world.clear_attack_events();

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

                for (const sim::AttackEvent& fx : effects) {
                    if (fx.to.z != center.z ||
                        std::abs(fx.to.x - center.x) > sim::kAoiHalfX ||
                        std::abs(fx.to.y - center.y) > sim::kAoiHalfY) {
                        continue;
                    }
                    std::uint8_t effect_buffer[32];
                    core::BitWriter effect_writer(effect_buffer,
                                                 sizeof(effect_buffer));
                    net::write_effect(effect_writer,
                                      net::EffectMsg{fx.from, fx.to, fx.effect});
                    send_message(*transport, peer, effect_writer, effect_buffer,
                                 net::Channel::Unreliable);
                }

                sim::Snapshot snapshot;
                sim::build_snapshot(world, center, snapshot);

                std::uint8_t buffer[net::kMaxPacketBytes];
                core::BitWriter writer(buffer, sizeof(buffer));
                net::write_snapshot(writer, snapshot);
                send_message(*transport, peer, writer, buffer,
                             net::Channel::Unreliable);
            }
        }

        transport->flush();

        // Yield the core. Without this the loop spins at 100% between ticks.
        core::sleep_nanos(1'000'000ULL);
    }

    // Graceful shutdown. Without this a restart threw away up to save_every_seconds
    // of everybody's progress, which is the sort of thing players notice long before
    // anyone reads the log.
    LOG_INFO("shutting down; saving %zu connected player(s)", connections.size());
    for (const auto& [peer, connection] : connections) {
        if (connection.welcomed) {
            persist(players_db, world, connection);
        }
    }
    return 0;
}

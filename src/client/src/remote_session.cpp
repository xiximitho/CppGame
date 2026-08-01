#include <cstdio>

#include "client/content.hpp"
#include "client/session.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "net/protocol.hpp"
#include "sim/content_blob.hpp"
#include "net/transport.hpp"

namespace client {
namespace {

/// Receives the world from a server.
///
/// Holds no simulation. There is no client-side prediction here yet: with
/// tile-stepped movement the visible latency is one step start, which is tolerable
/// on a LAN and is the honest baseline to measure against before adding
/// prediction. See docs/architecture.md for what prediction would need to touch.
class RemoteSession final : public Session {
public:
    RemoteSession(std::unique_ptr<net::ITransport> transport, std::string name,
                  sim::VocationId vocation, sim::COutfit outfit)
        : transport_(std::move(transport)),
          player_name_(std::move(name)),
          vocation_(vocation),
          outfit_(outfit) {}

    void update() override {
        if (transport_ == nullptr) {
            return;
        }

        net::Event event;
        while (transport_->poll(event)) {
            switch (event.type) {
                case net::EventType::Connected:
                    server_peer_ = event.peer;
                    send_hello();
                    break;

                case net::EventType::Disconnected:
                    LOG_WARN("disconnected from server");
                    alive_ = false;
                    status_ = "disconnected";
                    break;

                case net::EventType::Data:
                    handle_packet(event.data);
                    break;

                case net::EventType::None:
                    break;
            }
        }

        flush_pending_input();
    }

    void request_walk(sim::Direction dir) override {
        pending_walk_ = true;
        pending_dir_ = dir;
    }

    void request_move_to(sim::TilePos target) override {
        if (transport_ == nullptr || view_.local_id == sim::kInvalidNetId) {
            return;
        }

        std::uint8_t buffer[64];
        core::BitWriter writer(buffer, sizeof(buffer));
        net::write_move_to(writer, net::MoveToMsg{target});
        if (writer.overflowed()) {
            return;
        }

        // Reliable: a discrete command, not a sampled state. Dropping it means the
        // player clicks and nothing happens, which is the one outcome to avoid.
        transport_->send(server_peer_, buffer, writer.bytes_written(),
                         net::Channel::Reliable);
        transport_->flush();
    }

    void request_attack(sim::NetId target) override {
        send_reliable([&](core::BitWriter& writer) {
            net::write_attack(writer, net::AttackMsg{target});
        });
    }

    void request_equip(sim::ItemTypeId item) override {
        send_reliable([&](core::BitWriter& writer) {
            net::write_equip(writer, net::EquipMsg{item});
        });
    }

    void request_unequip(sim::EquipSlot slot) override {
        send_reliable([&](core::BitWriter& writer) {
            net::write_unequip(
                writer, net::UnequipMsg{static_cast<std::uint8_t>(slot)});
        });
    }

    void request_loot_take(sim::TilePos /*corpse_tile*/,
                           std::size_t /*index*/) override {
        // Corpses are not on the wire yet (L2). Solo has the full path.
    }

    void request_cast_spell(sim::SpellId spell, sim::NetId target) override {
        send_reliable([&](core::BitWriter& writer) {
            net::write_cast_spell(
                writer, net::CastSpellMsg{spell, target});
        });
    }

    template <typename Write>
    void send_reliable(Write&& write) {
        if (transport_ == nullptr || view_.local_id == sim::kInvalidNetId) {
            return;
        }
        std::uint8_t buffer[64];
        core::BitWriter writer(buffer, sizeof(buffer));
        write(writer);
        if (writer.overflowed()) {
            return;
        }
        // Reliable: a discrete command the server must not miss.
        transport_->send(server_peer_, buffer, writer.bytes_written(),
                         net::Channel::Reliable);
        transport_->flush();
    }

    const WorldView& view() const override { return view_; }

    std::vector<sim::AttackEvent> drain_effects() override {
        std::vector<sim::AttackEvent> out;
        out.swap(effects_buffer_);
        return out;
    }

    std::string status_text() const override {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
                      "net %s | tick %u | %zu actors | %d chunks",
                      status_.c_str(), view_.tick, view_.actors.size(),
                      chunks_received_);
        return std::string(buffer);
    }

    bool alive() const override { return alive_; }

private:
    void send_hello() {
        status_ = "handshaking";

        std::uint8_t buffer[net::kMaxPacketBytes];
        core::BitWriter writer(buffer, sizeof(buffer));

        net::HelloMsg hello;
        hello.protocol = net::kProtocolVersion;
        hello.name = player_name_;
        // The remote session has no use for item rules itself — the server owns
        // them — but it must prove its content matches, or an item edited on one
        // side silently changes what blocks and how hard things hit.
        hello.content_hash = sim::content_hash(load_item_catalogue());
        hello.vocation = vocation_;
        hello.outfit_head = outfit_.head;
        hello.outfit_body = outfit_.body;
        hello.outfit_legs = outfit_.legs;
        hello.outfit_feet = outfit_.feet;
        net::write_hello(writer, hello);

        if (writer.overflowed()) {
            LOG_ERROR("hello packet overflowed");
            return;
        }
        transport_->send(server_peer_, buffer, writer.bytes_written(),
                         net::Channel::Reliable);
        transport_->flush();
    }

    void flush_pending_input() {
        if (!pending_walk_ || view_.local_id == sim::kInvalidNetId) {
            return;
        }

        // Rate limited: holding a key must not emit one packet per rendered
        // frame. One per simulation tick is the most the server can act on.
        const std::uint64_t now = core::now_nanos();
        constexpr std::uint64_t kMinInterval = 1'000'000'000ULL / sim::kSimHz;
        if (now - last_input_sent_ < kMinInterval) {
            return;
        }
        last_input_sent_ = now;
        pending_walk_ = false;

        std::uint8_t buffer[64];
        core::BitWriter writer(buffer, sizeof(buffer));

        net::InputMsg input;
        input.client_tick = view_.tick;
        input.walk = true;
        input.dir = pending_dir_;
        net::write_input(writer, input);

        transport_->send(server_peer_, buffer, writer.bytes_written(),
                         net::Channel::Unreliable);
    }

    void handle_packet(const std::vector<std::uint8_t>& data) {
        core::BitReader reader(data.data(), data.size());

        switch (net::read_msg_id(reader)) {
            case net::MsgId::S2C_Welcome: {
                net::WelcomeMsg welcome;
                if (!net::read_welcome(reader, welcome)) {
                    LOG_WARN("malformed welcome");
                    return;
                }
                view_.map = sim::TileMap(welcome.map_width, welcome.map_height,
                                        welcome.map_floors);
                view_.local_id = welcome.your_id;
                view_.tick = welcome.tick;
                view_.ready = true;
                status_ = "connected";
                LOG_INFO("welcome: id=%u map=%ux%ux%u spawn=(%d,%d,%d)",
                         welcome.your_id,
                         static_cast<unsigned>(welcome.map_width),
                         static_cast<unsigned>(welcome.map_height),
                         static_cast<unsigned>(welcome.map_floors),
                         welcome.spawn.x, welcome.spawn.y,
                         static_cast<int>(welcome.spawn.z));
                break;
            }

            case net::MsgId::S2C_Reject: {
                net::RejectMsg reject;
                if (net::read_reject(reader, reject)) {
                    LOG_ERROR("server rejected us: %s", reject.reason.c_str());
                    status_ = "rejected: " + reject.reason;
                }
                alive_ = false;
                break;
            }

            case net::MsgId::S2C_Snapshot: {
                sim::Snapshot snapshot;
                if (!net::read_snapshot(reader, snapshot)) {
                    LOG_WARN("malformed snapshot");
                    return;
                }
                // Unreliable channel: a snapshot older than the newest one seen
                // is stale and must be discarded, not applied.
                if (view_.ready && snapshot.tick < view_.tick) {
                    ++stale_snapshots_;
                    return;
                }
                view_.tick = snapshot.tick;
                view_.actors = std::move(snapshot.actors);
                if (++snapshots_received_ == 1) {
                    LOG_INFO("first snapshot: tick %u, %zu actors visible",
                             snapshot.tick, view_.actors.size());
                }
                break;
            }

            case net::MsgId::S2C_MapChunk: {
                net::MapChunkMsg chunk;
                if (!net::read_map_chunk(reader, chunk)) {
                    LOG_WARN("malformed map chunk");
                    return;
                }
                apply_chunk(chunk);
                break;
            }

            case net::MsgId::S2C_Effect: {
                net::EffectMsg effect;
                if (net::read_effect(reader, effect)) {
                    effects_buffer_.push_back(
                        sim::AttackEvent{effect.from, effect.to, effect.effect});
                }
                break;
            }

            case net::MsgId::C2S_Hello:
            case net::MsgId::C2S_Input:
            case net::MsgId::C2S_MoveTo:
            case net::MsgId::C2S_Attack:
            case net::MsgId::C2S_Equip:
            case net::MsgId::C2S_Unequip:
            case net::MsgId::C2S_CastSpell:
            case net::MsgId::Invalid:
                // Client-to-server ids arriving here mean a confused or hostile
                // peer; ignoring is the correct response.
                break;
        }
    }

    void apply_chunk(const net::MapChunkMsg& chunk) {
        for (int row = 0; row < net::kChunkSize; ++row) {
            for (int col = 0; col < net::kChunkSize; ++col) {
                const auto index =
                    static_cast<std::size_t>(row * net::kChunkSize + col);
                if (index >= chunk.tiles.size()) {
                    continue;
                }
                const sim::TilePos pos{
                    static_cast<std::int16_t>(chunk.chunk_x + col),
                    static_cast<std::int16_t>(chunk.chunk_y + row), chunk.z};
                if (!view_.map.in_bounds(pos)) {
                    continue;
                }
                const sim::Tile& tile = chunk.tiles[index];
                view_.map.set_ground(pos, tile.ground);
                view_.map.set_object(pos, tile.object, tile.blocking);
            }
        }
        // First chunk at INFO because "did map streaming work at all" is the
        // question you actually ask; the rest sparsely at DEBUG, which is also how
        // you spot a server redundantly resending chunks.
        ++chunks_received_;
        if (chunks_received_ == 1) {
            LOG_INFO("first map chunk: (%d,%d,%d)", chunk.chunk_x, chunk.chunk_y,
                     static_cast<int>(chunk.z));
        } else if (chunks_received_ % 16 == 0) {
            LOG_DEBUG("map chunks received: %d", chunks_received_);
        }
    }

    std::unique_ptr<net::ITransport> transport_;
    std::string  player_name_;
    sim::VocationId vocation_ = sim::vocations::kKnight;
    sim::COutfit    outfit_{};
    net::PeerId  server_peer_ = net::kInvalidPeer;

    WorldView   view_;
    std::vector<sim::AttackEvent> effects_buffer_;
    std::string status_ = "connecting";
    bool        alive_ = true;
    int         chunks_received_ = 0;
    int         snapshots_received_ = 0;
    /// Out-of-order arrivals on the unreliable channel. A rising count here means
    /// the network is reordering, not that anything is broken.
    int stale_snapshots_ = 0;

    bool           pending_walk_ = false;
    sim::Direction pending_dir_ = sim::Direction::South;
    std::uint64_t  last_input_sent_ = 0;
};

}  // namespace

std::unique_ptr<Session> make_remote_session(const std::string& host,
                                             std::uint16_t port,
                                             const std::string& player_name,
                                             sim::VocationId vocation,
                                             sim::COutfit outfit) {
    auto transport = net::create_client(host, port);
    if (transport == nullptr) {
        return nullptr;
    }
    return std::make_unique<RemoteSession>(std::move(transport), player_name,
                                           vocation, outfit);
}

}  // namespace client

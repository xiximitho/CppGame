#include <enet/enet.h>

#include <unordered_map>

#include "core/log.hpp"
#include "net/transport.hpp"

namespace net {
namespace {

/// ENet needs one global init/teardown. Reference counted so a process running
/// both a client and an embedded server does not deinitialise out from under one
/// of them.
class EnetGlobal {
public:
    static bool acquire() {
        if (ref_count_ == 0) {
            if (enet_initialize() != 0) {
                LOG_ERROR("enet_initialize() failed");
                return false;
            }
        }
        ++ref_count_;
        return true;
    }

    static void release() {
        if (ref_count_ > 0 && --ref_count_ == 0) {
            enet_deinitialize();
        }
    }

private:
    static int ref_count_;
};

int EnetGlobal::ref_count_ = 0;

class EnetTransport final : public ITransport {
public:
    EnetTransport(ENetHost* host, bool is_server)
        : host_(host), is_server_(is_server) {}

    ~EnetTransport() override {
        if (host_ != nullptr) {
            // Give in-flight disconnects a moment to leave, otherwise peers only
            // notice via timeout.
            for (auto& [id, peer] : peers_) {
                enet_peer_disconnect(peer, 0);
            }
            enet_host_flush(host_);
            enet_host_destroy(host_);
        }
        EnetGlobal::release();
    }

    bool poll(Event& out) override {
        out.type = EventType::None;
        out.peer = kInvalidPeer;
        out.data.clear();

        ENetEvent event;
        const int result = enet_host_service(host_, &event, 0);
        if (result < 0) {
            LOG_ERROR("enet_host_service() failed");
            return false;
        }
        if (result == 0) {
            return false;
        }

        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                const PeerId id = next_peer_id_++;
                // ENet reuses ENetPeer slots, so the stable id lives in the
                // peer's user data instead of being derived from the pointer.
                event.peer->data =
                    reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
                peers_[id] = event.peer;
                connected_ = true;

                out.type = EventType::Connected;
                out.peer = id;
                return true;
            }

            case ENET_EVENT_TYPE_DISCONNECT: {
                const PeerId id = peer_id_of(event.peer);
                peers_.erase(id);
                event.peer->data = nullptr;
                if (!is_server_) {
                    connected_ = false;
                }

                out.type = EventType::Disconnected;
                out.peer = id;
                return true;
            }

            case ENET_EVENT_TYPE_RECEIVE: {
                out.type = EventType::Data;
                out.peer = peer_id_of(event.peer);
                out.data.assign(event.packet->data,
                                event.packet->data + event.packet->dataLength);
                enet_packet_destroy(event.packet);
                return true;
            }

            case ENET_EVENT_TYPE_NONE:
            default:
                return false;
        }
    }

    void send(PeerId peer, const void* data, std::size_t length,
              Channel channel) override {
        const auto it = peers_.find(peer);
        if (it == peers_.end() || length == 0) {
            return;
        }
        ENetPacket* packet = enet_packet_create(data, length, packet_flags(channel));
        if (packet == nullptr) {
            return;
        }
        if (enet_peer_send(it->second, static_cast<enet_uint8>(channel), packet) < 0) {
            // enet_peer_send takes ownership only on success.
            enet_packet_destroy(packet);
        }
    }

    void broadcast(const void* data, std::size_t length, Channel channel) override {
        if (length == 0 || peers_.empty()) {
            return;
        }
        ENetPacket* packet = enet_packet_create(data, length, packet_flags(channel));
        if (packet == nullptr) {
            return;
        }
        enet_host_broadcast(host_, static_cast<enet_uint8>(channel), packet);
    }

    void disconnect(PeerId peer) override {
        const auto it = peers_.find(peer);
        if (it != peers_.end()) {
            enet_peer_disconnect(it->second, 0);
        }
    }

    void flush() override { enet_host_flush(host_); }

    bool connected() const override { return is_server_ || connected_; }

    std::size_t peer_count() const override { return peers_.size(); }

private:
    static enet_uint32 packet_flags(Channel channel) {
        // ENet's default (no flags) is unreliable but sequenced, which is exactly
        // what a snapshot wants: never resent, never delivered out of order.
        return channel == Channel::Reliable
                   ? static_cast<enet_uint32>(ENET_PACKET_FLAG_RELIABLE)
                   : 0U;
    }

    static PeerId peer_id_of(const ENetPeer* peer) {
        if (peer == nullptr || peer->data == nullptr) {
            return kInvalidPeer;
        }
        return static_cast<PeerId>(reinterpret_cast<std::uintptr_t>(peer->data));
    }

    ENetHost* host_ = nullptr;
    bool      is_server_ = false;
    bool      connected_ = false;
    PeerId    next_peer_id_ = 1;
    std::unordered_map<PeerId, ENetPeer*> peers_;
};

}  // namespace

std::unique_ptr<ITransport> create_server(std::uint16_t port,
                                         std::size_t max_peers) {
    if (!EnetGlobal::acquire()) {
        return nullptr;
    }

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;

    ENetHost* host = enet_host_create(&address, max_peers, kChannelCount, 0, 0);
    if (host == nullptr) {
        LOG_ERROR("could not bind UDP port %u (already in use?)",
                  static_cast<unsigned>(port));
        EnetGlobal::release();
        return nullptr;
    }

    LOG_INFO("listening on UDP %u (max %zu peers)", static_cast<unsigned>(port),
             max_peers);
    return std::make_unique<EnetTransport>(host, true);
}

std::unique_ptr<ITransport> create_client(const std::string& host_name,
                                         std::uint16_t port) {
    if (!EnetGlobal::acquire()) {
        return nullptr;
    }

    ENetHost* host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    if (host == nullptr) {
        LOG_ERROR("could not create client socket");
        EnetGlobal::release();
        return nullptr;
    }

    ENetAddress address{};
    address.port = port;
    if (enet_address_set_host(&address, host_name.c_str()) != 0) {
        LOG_ERROR("could not resolve host '%s'", host_name.c_str());
        enet_host_destroy(host);
        EnetGlobal::release();
        return nullptr;
    }

    if (enet_host_connect(host, &address, kChannelCount, 0) == nullptr) {
        LOG_ERROR("no available peer slot for connection");
        enet_host_destroy(host);
        EnetGlobal::release();
        return nullptr;
    }

    LOG_INFO("connecting to %s:%u", host_name.c_str(), static_cast<unsigned>(port));
    return std::make_unique<EnetTransport>(host, false);
}

}  // namespace net

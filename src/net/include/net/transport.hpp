#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace net {

using PeerId = std::uint32_t;
constexpr PeerId kInvalidPeer = 0;

enum class EventType : std::uint8_t {
    None,
    Connected,
    Disconnected,
    Data,
};

struct Event {
    EventType                 type = EventType::None;
    PeerId                    peer = kInvalidPeer;
    std::vector<std::uint8_t> data;
};

/// Reliable ordered for anything that must arrive exactly once (login, map
/// chunks, chat). Unreliable sequenced for state that a newer packet supersedes
/// (snapshots) — resending a stale world state is worse than dropping it.
enum class Channel : std::uint8_t {
    Reliable   = 0,
    Unreliable = 1,
};

constexpr std::size_t kChannelCount = 2;

/// The seam between the game and whatever moves bytes.
///
/// ENet is the implementation today. When this needs DTLS, IPv6 and Steam
/// relaying, GameNetworkingSockets becomes a second implementation of this same
/// interface and no game code changes. That is the entire reason the abstraction
/// exists — see docs/architecture.md.
class ITransport {
public:
    virtual ~ITransport() = default;

    ITransport() = default;
    ITransport(const ITransport&) = delete;
    ITransport& operator=(const ITransport&) = delete;
    ITransport(ITransport&&) = delete;
    ITransport& operator=(ITransport&&) = delete;

    /// Pumps the network and returns one event. Call in a loop until it returns
    /// false; that is also what drives ENet's internal timers, so it must be
    /// called every frame even when no events are expected.
    virtual bool poll(Event& out) = 0;

    virtual void send(PeerId peer, const void* data, std::size_t length,
                      Channel channel) = 0;
    virtual void broadcast(const void* data, std::size_t length,
                           Channel channel) = 0;

    virtual void disconnect(PeerId peer) = 0;

    /// Pushes queued packets out now instead of on the next poll().
    virtual void flush() = 0;

    /// Client side: whether the session is established. Always true for servers.
    virtual bool connected() const = 0;

    virtual std::size_t peer_count() const = 0;
};

/// Listens on `port`. Returns nullptr when the socket cannot be bound.
std::unique_ptr<ITransport> create_server(std::uint16_t port,
                                          std::size_t max_peers);

/// Starts connecting to `host`:`port`. Returns immediately — the handshake
/// completes asynchronously and surfaces as an EventType::Connected from poll().
/// Returns nullptr only when the address cannot be resolved.
std::unique_ptr<ITransport> create_client(const std::string& host,
                                          std::uint16_t port);

}  // namespace net

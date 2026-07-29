#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sim/snapshot.hpp"
#include "sim/tile_map.hpp"
#include "sim/types.hpp"

namespace client {

/// Everything the renderer is allowed to know about the world.
///
/// Note what is absent: no entt::registry, no World. The renderer draws a list of
/// ActorState and a TileMap, and cannot reach into simulation internals. That is
/// what lets the exact same rendering code serve both a local simulation and a
/// remote server that only ever sends this much.
struct WorldView {
    sim::TileMap                 map;
    std::vector<sim::ActorState> actors;
    sim::NetId                   local_id = sim::kInvalidNetId;
    sim::Tick                    tick = 0;
    /// False until the map dimensions and the local actor id are known.
    bool ready = false;
};

/// Where the world comes from. Solo runs the simulation in-process; Remote
/// receives it. The client cannot tell the difference downstream of here, which
/// is what keeps single-player from quietly growing rules the server does not
/// enforce.
class Session {
public:
    Session() = default;
    virtual ~Session() = default;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    /// Called once per frame: advances the local simulation or pumps the socket.
    virtual void update() = 0;

    /// Player intent. Rejected moves are silently dropped by the simulation.
    virtual void request_walk(sim::Direction dir) = 0;

    virtual const WorldView& view() const = 0;

    /// Short human-readable state for the window title / HUD.
    virtual std::string status_text() const = 0;

    /// False once the session is unrecoverable (disconnected, rejected).
    virtual bool alive() const = 0;
};

/// In-process simulation. `wanderers` extra actors are spawned so there is
/// something to watch besides the player.
std::unique_ptr<Session> make_solo_session(std::uint64_t seed, int wanderers);

/// Connects to a server. Returns nullptr when the address cannot be resolved;
/// connection failures after that surface through alive().
std::unique_ptr<Session> make_remote_session(const std::string& host,
                                             std::uint16_t port,
                                             const std::string& player_name);

}  // namespace client

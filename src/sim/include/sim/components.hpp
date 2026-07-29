#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sim/types.hpp"

namespace sim {

/// Where an actor stands and which way it looks. `facing` is simulation state,
/// not presentation: in a tile MMO turning is an action with its own cost, and
/// attacks and line of sight depend on it.
struct CPosition {
    TilePos   tile;
    Direction facing = Direction::South;
};

/// Present only while a step is in flight. Its absence is what "standing still"
/// means, so a walking query is a single component lookup rather than a flag
/// test, and the ECS view iterates only the actors that are actually moving.
struct CWalk {
    TilePos   from;
    TilePos   to;
    Tick      start_tick = 0;
    Tick      end_tick   = 0;
    Direction dir        = Direction::South;
};

/// Identity that survives across the wire. Every entity the client can see has
/// one; purely server-side entities (spawners, triggers) do not.
struct CActor {
    NetId         net_id     = kInvalidNetId;
    std::uint16_t appearance = 0;
    /// Ticks per cardinal step. Lower is faster.
    Tick step_ticks = kDefaultStepTicks;
};

struct CHealth {
    std::int32_t hp     = 100;
    std::int32_t max_hp = 100;
};

/// Attached to the one actor a connected player drives. `peer` is the transport
/// handle; sim/ treats it as an opaque number and never calls into net/.
struct CPlayer {
    std::uint32_t peer = 0;
    std::string   name;
    /// Last tile the map streamer sent chunks around, so the server knows when
    /// the player has moved far enough to need more of the world.
    TilePos last_streamed_center{-32767, -32767, 0};
};

/// Marks an entity the server moves on its own. Kept minimal on purpose: real
/// AI belongs in its own system once behaviour exists.
struct CWanderer {
    Tick next_decision_tick = 0;
};

/// An actor walking a precomputed route, one step per tile.
///
/// Present only while following a path; its absence means "not going anywhere",
/// the same way CWalk's absence means "standing still". The path is planned
/// ignoring other actors, so a step can be refused when someone is in the way —
/// `blocked_ticks` counts how long that has been true so a permanently blocked
/// follower gives up instead of shoving forever.
struct CPathFollow {
    std::vector<TilePos> path;
    std::size_t          next = 0;
    Tick                 blocked_ticks = 0;
};

}  // namespace sim

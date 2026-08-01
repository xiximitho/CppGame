#pragma once

#include <cstdint>

// Fundamental value types of the simulation.
//
// Positions are integer tile coordinates, never floats: movement is discrete
// tile-to-tile stepping (Tibia style), and the smooth glide you see on screen is
// the client interpolating between two integer tiles. That choice is what makes
// the netcode cheap — a moving actor is "from tile A, to tile B, started at tick
// T", which is a handful of bytes and needs no correction stream.

namespace sim {

using Tick  = std::uint32_t;
using NetId = std::uint32_t;

/// 0 is reserved for "no actor".
constexpr NetId kInvalidNetId = 0;

/// Simulation rate. Fixed forever: it is baked into every tick number that
/// crosses the wire. Rendering runs at display rate and interpolates.
constexpr int   kSimHz = 30;
constexpr float kSimDt = 1.0F / static_cast<float>(kSimHz);

/// Ticks to walk one cardinal tile at default speed (9 / 30Hz = 300 ms).
constexpr Tick kDefaultStepTicks = 9;

/// Diagonal steps cover more ground, so they take longer. 3/2 keeps effective
/// speed roughly uniform; raise it if you want Tibia's heavier diagonal cost.
constexpr Tick step_ticks_for_diagonal(Tick cardinal_ticks) {
    return (cardinal_ticks * 3U) / 2U;
}

/// Combat timing and damage. Placeholders to be tuned; damage becomes data-driven
/// once items carry weapon/armour stats (see docs/combat.md).
constexpr Tick         kAttackCooldownTicks = static_cast<Tick>(kSimHz);      // ~1 swing/s
constexpr Tick         kRespawnTicks        = static_cast<Tick>(kSimHz * 3);  // ~3 s
constexpr std::int32_t kBaseMeleeDamage     = 18;

/// Area of interest half-extents, in tiles. The server only tells a player about
/// actors inside this box, which is what lets the world be larger than a
/// snapshot. A 23x17 window matches the classic tile-MMO viewport.
constexpr int kAoiHalfX = 11;
constexpr int kAoiHalfY = 8;

/// Directions in *grid* space. Note this is not screen space: on an isometric
/// 2:1 projection, grid NorthWest appears as straight up on screen. The mapping
/// from key presses to these values lives in the client input layer, which is
/// the only place that should know about the visual rotation.
enum class Direction : std::uint8_t {
    North     = 0,
    NorthEast = 1,
    East      = 2,
    SouthEast = 3,
    South     = 4,
    SouthWest = 5,
    West      = 6,
    NorthWest = 7,
};

constexpr int kDirectionCount = 8;

struct TileDelta {
    std::int8_t dx = 0;
    std::int8_t dy = 0;
};

constexpr TileDelta direction_delta(Direction dir) {
    switch (dir) {
        case Direction::North:     return {0, -1};
        case Direction::NorthEast: return {1, -1};
        case Direction::East:      return {1, 0};
        case Direction::SouthEast: return {1, 1};
        case Direction::South:     return {0, 1};
        case Direction::SouthWest: return {-1, 1};
        case Direction::West:      return {-1, 0};
        case Direction::NorthWest: return {-1, -1};
    }
    return {0, 0};
}

/// Odd-numbered directions are the diagonals.
constexpr bool is_diagonal(Direction dir) {
    return (static_cast<std::uint8_t>(dir) & 1U) != 0U;
}

/// A tile in the world. z is the floor index; 0 is the bottom floor and higher
/// values are floors above it, drawn later and offset upward on screen.
struct TilePos {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int8_t  z = 0;

    friend constexpr bool operator==(const TilePos& a, const TilePos& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
    friend constexpr bool operator!=(const TilePos& a, const TilePos& b) {
        return !(a == b);
    }
};

/// The tile one step away in `dir`, staying on the same floor.
constexpr TilePos tile_step(TilePos from, Direction dir) {
    const TileDelta delta = direction_delta(dir);
    return TilePos{static_cast<std::int16_t>(from.x + delta.dx),
                   static_cast<std::int16_t>(from.y + delta.dy),
                   from.z};
}

/// The direction from `from` to an adjacent tile `to`. Returns false when the two
/// are the same tile, not adjacent, or on different floors — which is how callers
/// detect that a stored path has gone stale.
constexpr bool direction_between(TilePos from, TilePos to, Direction& out) {
    if (from.z != to.z) {
        return false;
    }
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0)) {
        return false;
    }
    for (int i = 0; i < kDirectionCount; ++i) {
        const auto candidate = static_cast<Direction>(i);
        const TileDelta delta = direction_delta(candidate);
        if (delta.dx == dx && delta.dy == dy) {
            out = candidate;
            return true;
        }
    }
    return false;
}

/// Chebyshev distance on the same floor; -1 when the floors differ.
constexpr int tile_distance(TilePos a, TilePos b) {
    if (a.z != b.z) {
        return -1;
    }
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}

/// Whether `to` is within melee reach of `from`: adjacent (including diagonal) on
/// the same floor. One rule so the server and any client range hint never
/// disagree, the same discipline as sim::can_traverse for stepping.
constexpr bool in_melee_range(TilePos from, TilePos to) {
    return tile_distance(from, to) == 1;
}

/// Whether `to` is within `range` tiles of `from` (>=1), same floor. Melee passes
/// range 1; a bow passes its reach. Line of sight is not checked yet.
constexpr bool in_attack_range(TilePos from, TilePos to, int range) {
    const int distance = tile_distance(from, to);
    return distance >= 1 && distance <= range;
}

/// A warp: stepping onto `from` puts the actor on `to`, anywhere in the map.
///
/// The destination lives per *tile*, not on the item type, and that is the whole
/// difference from a stair. A stair is relative (z±1), so it can be a property of
/// the type and an author only paints the tile; a warp is absolute, and item ids
/// are shared contracts — two portals with different destinations use the same
/// sprite. So the author writes the pair in the map file and the World holds it
/// beside the grid. See the `portal` directive in map_io.hpp.
struct PortalSpec {
    TilePos from;
    TilePos to;
};

/// A swing that landed this tick, for the client to render an effect over: a
/// burst at `to` for melee (from == to reads as a hit), or a projectile from ->
/// to for ranged. Pure value type so it rides in a WorldView and on the wire.
struct AttackEvent {
    TilePos      from;
    TilePos      to;
    std::uint8_t effect = 0;  ///< ItemType::effect of the weapon used
};

/// Coarsest 8-way direction from `from` toward `to`, by the sign of dx/dy. Unlike
/// direction_between it works at any distance (for facing a ranged target);
/// returns South for the same tile.
constexpr Direction direction_towards(TilePos from, TilePos to) {
    const int dx = (to.x > from.x ? 1 : 0) - (to.x < from.x ? 1 : 0);
    const int dy = (to.y > from.y ? 1 : 0) - (to.y < from.y ? 1 : 0);
    for (int i = 0; i < kDirectionCount; ++i) {
        const auto candidate = static_cast<Direction>(i);
        const TileDelta delta = direction_delta(candidate);
        if (delta.dx == dx && delta.dy == dy) {
            return candidate;
        }
    }
    return Direction::South;
}

}  // namespace sim

#pragma once

#include <cstdint>
#include <vector>

#include "sim/types.hpp"

namespace sim {

/// Ground and object ids are indices into the client's tileset. The simulation
/// never interprets them beyond `blocking`; art is purely a client concern.
using TileId = std::uint16_t;

constexpr TileId kTileEmpty = 0;

struct Tile {
    TileId ground = kTileEmpty;
    TileId object = kTileEmpty;
    /// Cannot be walked onto. Kept as an explicit flag rather than derived from
    /// the object id so that the server never needs a tileset definition file.
    bool blocking = false;
};

/// A dense 3D grid of tiles. Dense on purpose: a tile is 6 bytes, so even a
/// 512x512x8 world is 12 MB, and dense indexing keeps lookups branch-free in the
/// movement and line-of-sight code that runs every tick.
class TileMap {
public:
    TileMap() = default;
    TileMap(int width, int height, int floors);

    int width() const { return width_; }
    int height() const { return height_; }
    int floors() const { return floors_; }

    bool in_bounds(TilePos pos) const;

    /// Out-of-bounds reads return a blocking empty tile so callers can skip
    /// bounds checks in hot loops without risking UB.
    const Tile& at(TilePos pos) const;
    Tile& mutable_at(TilePos pos);

    bool is_walkable(TilePos pos) const;

    void set_ground(TilePos pos, TileId ground);
    void set_object(TilePos pos, TileId object, bool blocking);

private:
    std::size_t index(TilePos pos) const;

    int width_ = 0;
    int height_ = 0;
    int floors_ = 0;
    std::vector<Tile> tiles_;
};

/// Whether the static geometry allows a step from `from` in `dir`. Checks the
/// destination is walkable and that a diagonal does not clip a wall corner.
///
/// Knows nothing about actors. Both World::can_enter and the pathfinder call this,
/// which is the point: if they used different rules, A* would happily return paths
/// that the movement code then refuses, and an actor would stall forever on a step
/// its own path told it to take.
bool can_traverse(const TileMap& map, TilePos from, Direction dir);

}  // namespace sim

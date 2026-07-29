#include "sim/tile_map.hpp"

#include <cassert>

namespace sim {
namespace {

/// Returned for every out-of-bounds access. Blocking so actors cannot walk off
/// the edge of the world without an explicit bounds test.
const Tile& out_of_bounds_tile() {
    static const Tile tile{kTileEmpty, kTileEmpty, true};
    return tile;
}

}  // namespace

TileMap::TileMap(int width, int height, int floors)
    : width_(width), height_(height), floors_(floors) {
    assert(width > 0 && height > 0 && floors > 0);
    tiles_.resize(static_cast<std::size_t>(width) *
                  static_cast<std::size_t>(height) *
                  static_cast<std::size_t>(floors));
}

std::size_t TileMap::index(TilePos pos) const {
    return (static_cast<std::size_t>(pos.z) * static_cast<std::size_t>(height_) +
            static_cast<std::size_t>(pos.y)) *
               static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(pos.x);
}

bool TileMap::in_bounds(TilePos pos) const {
    return pos.x >= 0 && pos.x < width_ && pos.y >= 0 && pos.y < height_ &&
           pos.z >= 0 && pos.z < floors_;
}

const Tile& TileMap::at(TilePos pos) const {
    if (!in_bounds(pos)) {
        return out_of_bounds_tile();
    }
    return tiles_[index(pos)];
}

Tile& TileMap::mutable_at(TilePos pos) {
    assert(in_bounds(pos));
    return tiles_[index(pos)];
}

bool TileMap::is_walkable(TilePos pos) const {
    const Tile& tile = at(pos);
    // A tile with no ground is a hole, not a floor: nothing to stand on.
    return tile.ground != kTileEmpty && !tile.blocking;
}

void TileMap::set_ground(TilePos pos, TileId ground) {
    if (!in_bounds(pos)) {
        return;
    }
    tiles_[index(pos)].ground = ground;
}

void TileMap::set_object(TilePos pos, TileId object, bool blocking) {
    if (!in_bounds(pos)) {
        return;
    }
    Tile& tile = tiles_[index(pos)];
    tile.object = object;
    tile.blocking = blocking;
}

}  // namespace sim

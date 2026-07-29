#pragma once

#include "sim/tile_map.hpp"

namespace sim::tiles {

// The placeholder tileset. These ids are the contract between the map generator
// and the client's atlas lookup; the simulation itself only cares about the
// `blocking` flag stored alongside them.
//
// When you replace the procedural art with real sprites, this is the list that
// grows into a data file loaded by the client. The server keeps needing nothing
// but the numbers.

constexpr TileId kGrass = 1;
constexpr TileId kDirt  = 2;
constexpr TileId kStone = 3;
constexpr TileId kWater = 4;

constexpr TileId kWall = 100;
constexpr TileId kTree = 101;

constexpr TileId kActor = 200;

}  // namespace sim::tiles

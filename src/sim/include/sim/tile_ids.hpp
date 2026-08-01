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

constexpr TileId kWall  = 100;
constexpr TileId kTree  = 101;
constexpr TileId kCrate = 102;

// Stairs. Walkable objects: the rule is "arriving here moves you a floor", so a
// blocking stair would be a stair nobody can use.
constexpr TileId kStairsUp   = 103;
constexpr TileId kStairsDown = 104;

// The warp mouth. Walkable for the same reason as a stair, and — unlike a stair —
// it carries no destination: that is per-tile map data (`portal` in docs/maps.md).
// The id is here so the generator and the client agree on which sprite to draw.
constexpr TileId kPortal = 105;

constexpr TileId kActor = 200;

// Equipment items. Not placed on the map like tiles; they live in inventories.
constexpr TileId kSword  = 300;
constexpr TileId kBow    = 301;
constexpr TileId kShield = 302;
constexpr TileId kHelmet = 303;
constexpr TileId kArmor  = 304;
constexpr TileId kLegs   = 305;
constexpr TileId kBoots  = 306;
constexpr TileId kRing   = 307;
constexpr TileId kAmulet = 308;
/// Caster weapon. Mage and Druid fight at full strength with it; others do not.
constexpr TileId kStaff  = 310;

}  // namespace sim::tiles

#pragma once

#include <cstdint>

#include "sim/item_type.hpp"
#include "sim/rng.hpp"
#include "sim/tile_map.hpp"

namespace sim {

struct MapGenSettings {
    int           width  = 96;
    int           height = 96;
    int           floors = 3;
    std::uint64_t seed   = 1337;
};

/// Placeholder world so there is something to walk around in before real map
/// data exists. Fully deterministic from the seed, which is why the client can
/// generate the same world for solo play that the server generates for network
/// play — see docs/architecture.md for why that is a scaffold shortcut and not
/// the shipping design.
///
/// `item_types` is the source of truth for whether a placed tile blocks: the
/// generator no longer hardcodes the blocking bool, it derives it from the
/// BlocksWalk flag of the ground and object it places (see docs/content.md).
TileMap generate_demo_map(const MapGenSettings& settings,
                          const ItemTypeRegistry& item_types);

/// A walkable tile on floor 0, chosen deterministically from `rng`. Falls back
/// to scanning the map when random picks keep landing on water or walls.
TilePos find_spawn_tile(const TileMap& map, Rng& rng);

}  // namespace sim

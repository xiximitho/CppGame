#pragma once

#include <cstdint>

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
TileMap generate_demo_map(const MapGenSettings& settings);

/// A walkable tile on floor 0, chosen deterministically from `rng`. Falls back
/// to scanning the map when random picks keep landing on water or walls.
TilePos find_spawn_tile(const TileMap& map, Rng& rng);

}  // namespace sim

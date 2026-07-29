#include "sim/map_gen.hpp"

#include "sim/tile_ids.hpp"

namespace sim {
namespace {

/// Places an object and derives the tile's blocking flag from item properties
/// instead of a hardcoded bool: a tile blocks if either its ground or its object
/// carries ItemFlag::BlocksWalk. Ground is read from the tile, so callers must
/// set it first (they all do). This reproduces exactly what the old explicit
/// bools said, but now the flag catalogue is the single source of truth.
void place_object(TileMap& map, TilePos pos, TileId object,
                  const ItemTypeRegistry& items) {
    const TileId ground = map.at(pos).ground;
    const bool blocking = items.get(ground).blocks_walk() ||
                          items.get(object).blocks_walk();
    map.set_object(pos, object, blocking);
}

void fill_ground(TileMap& map, int z, TileId ground) {
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                   static_cast<std::int16_t>(y),
                                   static_cast<std::int8_t>(z)},
                           ground);
        }
    }
}

void carve_lake(TileMap& map, Rng& rng, int z, const ItemTypeRegistry& items) {
    const int cx = rng.next_range(map.width() / 5, (map.width() * 4) / 5);
    const int cy = rng.next_range(map.height() / 5, (map.height() * 4) / 5);
    const int radius = rng.next_range(5, 10);

    for (int y = cy - radius; y <= cy + radius; ++y) {
        for (int x = cx - radius; x <= cx + radius; ++x) {
            const int dx = x - cx;
            const int dy = y - cy;
            // Squashed circle: on a 2:1 isometric projection a round lake in
            // tile space already reads as round on screen.
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            const TilePos pos{static_cast<std::int16_t>(x),
                              static_cast<std::int16_t>(y),
                              static_cast<std::int8_t>(z)};
            if (!map.in_bounds(pos)) {
                continue;
            }
            map.set_ground(pos, tiles::kWater);
            place_object(map, pos, kTileEmpty, items);
        }
    }
}

void carve_path(TileMap& map, Rng& rng, int z, const ItemTypeRegistry& items) {
    // A drunk walk across the map, laid down as dirt. Clears whatever it crosses
    // so paths stay walkable even through walls and water.
    int x = rng.next_range(0, map.width() - 1);
    int y = 0;
    while (y < map.height()) {
        const TilePos pos{static_cast<std::int16_t>(x),
                          static_cast<std::int16_t>(y),
                          static_cast<std::int8_t>(z)};
        if (map.in_bounds(pos)) {
            map.set_ground(pos, tiles::kDirt);
            place_object(map, pos, kTileEmpty, items);
        }
        if (rng.chance(0.35F)) {
            x += rng.chance(0.5F) ? 1 : -1;
            if (x < 1) x = 1;
            if (x > map.width() - 2) x = map.width() - 2;
        } else {
            ++y;
        }
    }
}

void build_room(TileMap& map, Rng& rng, int z, const ItemTypeRegistry& items) {
    const int w = rng.next_range(6, 12);
    const int h = rng.next_range(6, 12);
    const int x0 = rng.next_range(2, map.width() - w - 3);
    const int y0 = rng.next_range(2, map.height() - h - 3);

    const int door_side = rng.next_range(0, 3);
    const int door_at = rng.next_range(1, (door_side < 2 ? w : h) - 2);

    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            const bool on_edge =
                x == x0 || x == x0 + w - 1 || y == y0 || y == y0 + h - 1;
            const TilePos pos{static_cast<std::int16_t>(x),
                              static_cast<std::int16_t>(y),
                              static_cast<std::int8_t>(z)};
            if (!map.in_bounds(pos)) {
                continue;
            }
            map.set_ground(pos, tiles::kStone);
            if (!on_edge) {
                place_object(map, pos, kTileEmpty, items);
                continue;
            }

            const bool is_door =
                (door_side == 0 && y == y0 && x == x0 + door_at) ||
                (door_side == 1 && y == y0 + h - 1 && x == x0 + door_at) ||
                (door_side == 2 && x == x0 && y == y0 + door_at) ||
                (door_side == 3 && x == x0 + w - 1 && y == y0 + door_at);

            place_object(map, pos, is_door ? kTileEmpty : tiles::kWall, items);
        }
    }
}

void scatter_trees(TileMap& map, Rng& rng, int z, int count,
                   const ItemTypeRegistry& items) {
    for (int i = 0; i < count; ++i) {
        const TilePos pos{
            static_cast<std::int16_t>(rng.next_range(1, map.width() - 2)),
            static_cast<std::int16_t>(rng.next_range(1, map.height() - 2)),
            static_cast<std::int8_t>(z)};
        if (map.at(pos).ground == tiles::kGrass &&
            map.at(pos).object == kTileEmpty) {
            place_object(map, pos, tiles::kTree, items);
        }
    }
}

void scatter_crates(TileMap& map, Rng& rng, int z, int count,
                    const ItemTypeRegistry& items) {
    for (int i = 0; i < count; ++i) {
        const TilePos pos{
            static_cast<std::int16_t>(rng.next_range(1, map.width() - 2)),
            static_cast<std::int16_t>(rng.next_range(1, map.height() - 2)),
            static_cast<std::int8_t>(z)};
        if (map.at(pos).ground == tiles::kGrass &&
            map.at(pos).object == kTileEmpty) {
            place_object(map, pos, tiles::kCrate, items);
        }
    }
}

void wall_border(TileMap& map, int z, const ItemTypeRegistry& items) {
    for (int x = 0; x < map.width(); ++x) {
        for (const int y : {0, map.height() - 1}) {
            const TilePos pos{static_cast<std::int16_t>(x),
                              static_cast<std::int16_t>(y),
                              static_cast<std::int8_t>(z)};
            place_object(map, pos, tiles::kWall, items);
        }
    }
    for (int y = 0; y < map.height(); ++y) {
        for (const int x : {0, map.width() - 1}) {
            const TilePos pos{static_cast<std::int16_t>(x),
                              static_cast<std::int16_t>(y),
                              static_cast<std::int8_t>(z)};
            place_object(map, pos, tiles::kWall, items);
        }
    }
}

/// Floor 1 exists so the multi-floor rendering and the "hide the floors above
/// the player" rule are exercised from day one instead of being bolted on later.
void build_upper_platform(TileMap& map, Rng& rng,
                          const ItemTypeRegistry& items) {
    if (map.floors() < 2) {
        return;
    }
    const int w = rng.next_range(10, 16);
    const int h = rng.next_range(10, 16);
    const int x0 = rng.next_range(2, map.width() - w - 3);
    const int y0 = rng.next_range(2, map.height() - h - 3);

    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            const TilePos pos{static_cast<std::int16_t>(x),
                              static_cast<std::int16_t>(y), 1};
            if (!map.in_bounds(pos)) {
                continue;
            }
            map.set_ground(pos, tiles::kStone);
            const bool on_edge =
                x == x0 || x == x0 + w - 1 || y == y0 || y == y0 + h - 1;
            place_object(map, pos, on_edge ? tiles::kWall : kTileEmpty, items);
        }
    }
}

}  // namespace

TileMap generate_demo_map(const MapGenSettings& settings,
                          const ItemTypeRegistry& item_types) {
    TileMap map(settings.width, settings.height, settings.floors);
    Rng rng(settings.seed);

    fill_ground(map, 0, tiles::kGrass);

    for (int i = 0; i < 3; ++i) {
        carve_lake(map, rng, 0, item_types);
    }
    for (int i = 0; i < 5; ++i) {
        build_room(map, rng, 0, item_types);
    }
    for (int i = 0; i < 3; ++i) {
        carve_path(map, rng, 0, item_types);
    }
    scatter_trees(map, rng, 0, (settings.width * settings.height) / 60,
                  item_types);
    wall_border(map, 0, item_types);

    build_upper_platform(map, rng, item_types);

    // Kept last so it does not shift the RNG stream the rest of the map depends
    // on. Worked example for docs/sprites.md.
    scatter_crates(map, rng, 0, 40, item_types);

    return map;
}

TilePos find_spawn_tile(const TileMap& map, Rng& rng) {
    for (int attempt = 0; attempt < 512; ++attempt) {
        const TilePos pos{
            static_cast<std::int16_t>(rng.next_range(1, map.width() - 2)),
            static_cast<std::int16_t>(rng.next_range(1, map.height() - 2)), 0};
        if (map.is_walkable(pos)) {
            return pos;
        }
    }

    // Deterministic fallback so a pathological map still yields a valid spawn.
    for (int y = 1; y < map.height() - 1; ++y) {
        for (int x = 1; x < map.width() - 1; ++x) {
            const TilePos pos{static_cast<std::int16_t>(x),
                              static_cast<std::int16_t>(y), 0};
            if (map.is_walkable(pos)) {
                return pos;
            }
        }
    }

    return TilePos{1, 1, 0};
}

}  // namespace sim

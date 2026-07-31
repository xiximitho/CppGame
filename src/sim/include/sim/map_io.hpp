#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sim/item_type.hpp"
#include "sim/monster_type.hpp"
#include "sim/tile_map.hpp"
#include "sim/types.hpp"

// Text map format: authored maps, editable by hand and (later) by a simple
// editor. Pure parsing lives here so it can be shared by the client (which reads
// the file through platform::vfs, for Android) and the server (which reads it
// through <fstream>, since it links no SDL). No I/O in this header or its .cpp.
//
// Grammar, line-based, '#' in column 0 starts a comment, blank lines ignored
// (except inside a grid, which is read verbatim):
//
//   size <w> <h> <floors>
//   legend <char> <ground_id> [object_id]   # binds one grid char to tiles
//   spawn <char>                             # marks that char as the spawn tile
//   monster <x> <y> <z> <class_id>           # one authored mob (sim::monsters::)
//   spawner <x> <y> <z> <class_id> <count> <radius> <respawn_seconds>
//   floor <z>                                # the next <h> lines are the grid
//   <h rows of exactly-or-fewer-than-w chars>
//
// A space (or a row shorter than <w>) is void: no ground, an unwalkable hole,
// which is how the dark area outside a dungeon is drawn. Blocking is derived
// from the item catalogue exactly like the procedural generator, so water and
// walls block without the map file restating it.

namespace sim {

struct ParsedMap {
    TileMap                map;
    /// Set when the map defines a `spawn` char. Both the server and the solo
    /// session prefer it and fall back to a random walkable tile.
    std::optional<TilePos> spawn;
    /// Monsters the author placed, in file order. Spawning them is the caller's
    /// job (sim::spawn_authored_monsters): the parser only reads.
    std::vector<MonsterSpawn> monsters;
    /// Spawn points the author placed. Creating them is the caller's job too
    /// (sim::create_spawners). A `monster` line is one mob that never comes back; a
    /// `spawner` line is a population that does.
    std::vector<SpawnerSpec> spawners;
};

/// Parses `text` into a map. Returns std::nullopt and, when `error` is non-null,
/// a human-readable reason on any malformed input (bad size, unknown grid char,
/// truncated grid, ...). `items` supplies the blocking flags.
std::optional<ParsedMap> parse_text_map(const std::string& text,
                                        const ItemTypeRegistry& items,
                                        std::string* error = nullptr);

/// Serialises a map back to the text format — the inverse of parse_text_map,
/// used by the editor to save. Pure. Builds a legend automatically (one char per
/// distinct ground/object pair); `spawn`, when given, is written as '@'. A
/// parse of the result reproduces the same walkable geometry.
std::string write_text_map(const TileMap& map,
                           const std::optional<TilePos>& spawn = std::nullopt,
                           const std::vector<MonsterSpawn>& monsters = {},
                           const std::vector<SpawnerSpec>& spawners = {});

}  // namespace sim

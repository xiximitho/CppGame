#pragma once

#include <optional>
#include <string>

#include "sim/item_type.hpp"
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
    /// Set when the map defines a `spawn` char; a session may prefer it over a
    /// random walkable tile. Nothing forces its use yet.
    std::optional<TilePos> spawn;
};

/// Parses `text` into a map. Returns std::nullopt and, when `error` is non-null,
/// a human-readable reason on any malformed input (bad size, unknown grid char,
/// truncated grid, ...). `items` supplies the blocking flags.
std::optional<ParsedMap> parse_text_map(const std::string& text,
                                        const ItemTypeRegistry& items,
                                        std::string* error = nullptr);

}  // namespace sim

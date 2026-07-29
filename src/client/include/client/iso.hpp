#pragma once

#include <cmath>

#include "sim/types.hpp"

namespace client::iso {

// The isometric projection, in one place.
//
//   screen_x = (tile_x - tile_y) * TILE_W/2
//   screen_y = (tile_x + tile_y) * TILE_H/2 - floor_z * FLOOR_H
//
// A 2:1 tile (64x32) is the classic choice: the diagonals land on exact half-pixel
// slopes, so hand-drawn tile edges tessellate without seams and the inverse
// mapping below is exact rather than approximate.
//
// The reference point of a tile is its TOP VERTEX, not the top-left of its
// bounding box. That makes the projection formula above literal, and every
// sprite is then placed by an offset from that vertex (see AtlasEntry::origin).

constexpr int kTileWidth  = 64;
constexpr int kTileHeight = 32;

/// Vertical screen offset between two floors. Independent of tile height: it is
/// an art decision about how tall a storey looks.
constexpr int kFloorHeight = 24;

constexpr int kHalfTileWidth  = kTileWidth / 2;
constexpr int kHalfTileHeight = kTileHeight / 2;

struct ScreenPos {
    float x = 0.0F;
    float y = 0.0F;
};

/// Fractional tile coordinates are allowed and are how a walking actor is drawn
/// between two tiles.
inline ScreenPos tile_to_screen(float tile_x, float tile_y, int floor_z) {
    return ScreenPos{
        (tile_x - tile_y) * static_cast<float>(kHalfTileWidth),
        (tile_x + tile_y) * static_cast<float>(kHalfTileHeight) -
            static_cast<float>(floor_z * kFloorHeight)};
}

inline ScreenPos tile_to_screen(sim::TilePos pos) {
    return tile_to_screen(static_cast<float>(pos.x), static_cast<float>(pos.y),
                          pos.z);
}

/// Inverse projection: which tile of floor `floor_z` contains this screen point.
///
/// Exact for points inside a tile. Points landing precisely on a shared diamond
/// edge resolve to one of the neighbours; which one is unspecified and does not
/// matter for click targeting.
inline sim::TilePos screen_to_tile(float screen_x, float screen_y, int floor_z) {
    const float a = screen_x / static_cast<float>(kHalfTileWidth);
    const float b = (screen_y + static_cast<float>(floor_z * kFloorHeight)) /
                    static_cast<float>(kHalfTileHeight);

    return sim::TilePos{
        static_cast<std::int16_t>(std::floor((a + b) * 0.5F)),
        static_cast<std::int16_t>(std::floor((b - a) * 0.5F)),
        static_cast<std::int8_t>(floor_z)};
}

/// Draw-order layers within one tile.
enum class Layer : int {
    Ground = 0,
    Object = 1,
    Actor  = 2,
};

/// Sort key for the render queue.
///
/// Floors dominate absolutely. Within a floor, ground is a flat layer underneath
/// everything, and only objects and actors sort against each other along the
/// screen's back-to-front axis (tile_x + tile_y), then by layer.
///
/// Ground deliberately does NOT take part in positional sorting. A floor tile is
/// flat: it can never legitimately stand in front of something on a neighbouring
/// tile. Sorting it positionally made the ground of the tile an actor was walking
/// *into* sort above that actor — at half a step, an actor leaving (10,10) keys at
/// 2052 while the ground of (11,10) keys at 2100 — so the destination tile drew
/// over the sprite's lower half and its diamond edge visibly sliced through the
/// character for the whole step. Ground tiles tessellate exactly and never overlap
/// each other, so giving them all one key costs nothing.
///
/// This is still a painter's-algorithm key, correct for single-tile objects.
/// Objects wider or taller than one tile need a real topological sort; that is a
/// known limit, not an oversight, and it shows up the day a 2x2 building overlaps
/// an actor.
inline float depth_key(float tile_x, float tile_y, int floor_z, Layer layer) {
    const float floor_base = static_cast<float>(floor_z) * 1.0e6F;

    if (layer == Layer::Ground) {
        return floor_base;
    }

    // The +1 keeps the whole object/actor band strictly above every ground tile of
    // the same floor, leaving floor_base + 0.5 free for floor decals such as the
    // cursor highlight.
    return floor_base + 1.0F + (tile_x + tile_y) * 100.0F +
           static_cast<float>(static_cast<int>(layer));
}

}  // namespace client::iso

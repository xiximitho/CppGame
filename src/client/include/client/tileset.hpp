#pragma once

#include <array>
#include <unordered_map>

#include "client/renderer2d.hpp"
#include "sim/tile_map.hpp"
#include "sim/types.hpp"

namespace client {

/// One sprite inside the atlas.
struct AtlasEntry {
    Rect  uv;                  ///< normalised atlas coordinates
    float width = 0.0F;        ///< pixels
    float height = 0.0F;
    /// Offset from the tile's TOP VERTEX (see iso.hpp) to this sprite's top-left.
    /// This is what makes a 64x64 wall and a 32x48 actor both land correctly on a
    /// 64x32 tile without per-call special cases.
    float origin_x = 0.0F;
    float origin_y = 0.0F;
    bool  valid = false;
};

/// The atlas plus the id-to-sprite lookup.
///
/// Everything here is generated in code. That is deliberate for the scaffold: no
/// binary assets means no image decoder dependency, no asset pipeline, and a
/// clone-and-build that cannot fail because a PNG is missing. Replacing this with
/// a packed atlas loaded through platform::vfs is a self-contained change — the
/// rest of the renderer only ever sees AtlasEntry.
class Tileset {
public:
    /// Builds the placeholder atlas and uploads it. Returns an object whose
    /// texture() is invalid if upload failed.
    static Tileset build_procedural(Renderer2D& renderer);

    TextureHandle texture() const { return texture_; }

    /// Missing ids return an entry with valid == false, which callers skip.
    const AtlasEntry& ground(sim::TileId id) const;
    const AtlasEntry& object(sim::TileId id) const;

    /// `facing` is a grid direction; the frame chosen already accounts for the
    /// isometric rotation.
    const AtlasEntry& actor(sim::Direction facing) const;

    /// Diamond outline drawn under the mouse cursor.
    const AtlasEntry& highlight() const { return highlight_; }

private:
    TextureHandle texture_;
    std::unordered_map<sim::TileId, AtlasEntry> ground_;
    std::unordered_map<sim::TileId, AtlasEntry> object_;
    std::array<AtlasEntry, 8> actor_frames_{};
    AtlasEntry highlight_{};
    AtlasEntry invalid_{};
};

}  // namespace client

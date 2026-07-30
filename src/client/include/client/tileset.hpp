#pragma once

#include <array>
#include <string>
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

    /// Loads the packed atlas (tilesets/atlas.png + tilesets/atlas.txt) through
    /// platform::vfs, or falls back to build_procedural() when the files are
    /// missing or fail to decode. This is what main() calls: a clone with no art
    /// still runs, and dropping a real PNG in switches the whole look with no
    /// code change. atlas.txt is where a sprite is bound to an id — see the file
    /// itself and docs/content.md.
    static Tileset load(Renderer2D& renderer);

    TextureHandle texture() const { return texture_; }

    /// Missing ids return an entry with valid == false, which callers skip.
    const AtlasEntry& ground(sim::TileId id) const;
    const AtlasEntry& object(sim::TileId id) const;

    /// `facing` is a grid direction; the frame chosen already accounts for the
    /// isometric rotation.
    const AtlasEntry& actor(sim::Direction facing) const;

    /// Diamond outline drawn under the mouse cursor.
    const AtlasEntry& highlight() const { return highlight_; }

    /// A solid white texel. Tinted, it draws filled rectangles (UI panels,
    /// selection frames) through the same batched path as sprites.
    const AtlasEntry& solid() const { return solid_; }

    /// Inventory icon for an item id (invalid when the atlas has none).
    const AtlasEntry& icon(sim::TileId id) const;

    /// Attack-effect sprite by id (see sim ItemType::effect); invalid if absent.
    const AtlasEntry& effect(std::uint8_t id) const;

private:
    /// Fills `out` from the text metadata; returns false if nothing parsed. Kept
    /// a member so it can populate the private lookup tables.
    static bool parse_atlas_meta(const std::string& text, int atlas_w,
                                 int atlas_h, Tileset& out);

    TextureHandle texture_;
    std::unordered_map<sim::TileId, AtlasEntry> ground_;
    std::unordered_map<sim::TileId, AtlasEntry> object_;
    std::array<AtlasEntry, 8> actor_frames_{};
    AtlasEntry highlight_{};
    AtlasEntry solid_{};
    std::unordered_map<sim::TileId, AtlasEntry>      icons_;
    std::unordered_map<std::uint8_t, AtlasEntry>     effects_;
    AtlasEntry invalid_{};
};

}  // namespace client

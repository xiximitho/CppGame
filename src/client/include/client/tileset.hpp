#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

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
    /// isometric rotation. `appearance` selects the sprite set (0 is the player,
    /// the rest come from `mob` lines); an appearance the atlas does not have
    /// falls back to 0, so a mob whose art is missing draws as a knight instead
    /// of vanishing.
    const AtlasEntry& actor(sim::Direction facing,
                            std::uint16_t appearance = 0) const;

    /// Diamond outline drawn under the mouse cursor.
    const AtlasEntry& highlight() const { return highlight_; }

    /// A solid white texel. Tinted, it draws filled rectangles (UI panels,
    /// selection frames) through the same batched path as sprites.
    const AtlasEntry& solid() const { return solid_; }

    /// Inventory icon for an item id (invalid when the atlas has none).
    const AtlasEntry& icon(sim::TileId id) const;

    /// Attack-effect sprite by id (see sim ItemType::effect); invalid if absent.
    const AtlasEntry& effect(std::uint8_t id) const;

    /// Atlas size in pixels. The editor's sprite picker needs it to draw the sheet
    /// and to lay a cell grid over it; nothing in the render path does, because every
    /// AtlasEntry already carries normalised uv.
    int atlas_width()  const { return atlas_width_; }
    int atlas_height() const { return atlas_height_; }

    /// One glyph of the bitmap font. Characters outside the table (and every
    /// atlas without a `font` line, including the procedural fallback) return an
    /// invalid entry, so text silently draws nothing rather than garbage.
    const AtlasEntry& glyph(char c) const;

    /// Font cell size in atlas pixels. The cell includes the 1px spacing, so
    /// advancing the pen by glyph_advance() needs no kerning table.
    float glyph_advance() const { return glyph_advance_; }
    float glyph_height()  const { return glyph_height_; }
    bool  has_font()      const { return !glyphs_.empty(); }

private:
    /// Fills `out` from the text metadata; returns false if nothing parsed. Kept
    /// a member so it can populate the private lookup tables.
    static bool parse_atlas_meta(const std::string& text, int atlas_w,
                                 int atlas_h, Tileset& out);

    TextureHandle texture_;
    int atlas_width_ = 0;
    int atlas_height_ = 0;
    std::unordered_map<sim::TileId, AtlasEntry> ground_;
    std::unordered_map<sim::TileId, AtlasEntry> object_;
    std::array<AtlasEntry, 8> actor_frames_{};
    /// One 8-direction set per non-zero appearance, from the atlas `mob` lines.
    std::unordered_map<std::uint16_t, std::array<AtlasEntry, 8>> mob_frames_;
    AtlasEntry highlight_{};
    AtlasEntry solid_{};
    std::unordered_map<sim::TileId, AtlasEntry>      icons_;
    std::unordered_map<std::uint8_t, AtlasEntry>     effects_;
    /// Dense, indexed by `character - glyph_first_`; empty when the atlas has no
    /// font. Dense because text costs one lookup per character per frame.
    std::vector<AtlasEntry> glyphs_;
    int   glyph_first_   = 0;
    float glyph_advance_ = 0.0F;
    float glyph_height_  = 0.0F;
    AtlasEntry invalid_{};
};

}  // namespace client

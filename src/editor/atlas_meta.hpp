#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "sim/item_type.hpp"

// Editing assets/tilesets/atlas.txt, the file that answers "which sprite is item
// 103?".
//
// This is the PRESENTATION half of an item and it lives on the client side only —
// it is not in content.db and not in the baked blob, which is what keeps the server
// from ever needing to know what anything looks like (docs/content.md).
//
// The functions here are pure string transformations so they can be unit tested
// without SDL and without touching a file: atlas.txt is hand-editable and
// hand-edited, and a writer that reformats or drops the parts it did not understand
// would quietly destroy someone's work. Every line the transformation does not
// target comes out byte for byte, comments included.
//
// Reading is Tileset::parse_atlas_meta's job; this is only the writing side.

namespace editor {

/// One sprite binding.
///
/// `origin` is the offset from the tile's top vertex to the sprite's top-left (see
/// docs/sprites.md) and applies to ground/object lines only — `item` lines are
/// inventory icons, drawn in UI space, and carry no origin. Which is why kind is
/// part of the record rather than inferred at write time.
struct AtlasBinding {
    std::string     kind;  ///< "ground", "object" or "item"
    sim::ItemTypeId id = sim::kItemNone;
    int             x = 0;
    int             y = 0;
    int             w = 0;
    int             h = 0;
    float           origin_x = 0.0F;
    float           origin_y = 0.0F;
};

/// Canonical origin for a kind, from docs/sprites.md: ground 64x32 is (-32, 0),
/// a 64x64 block is (-32, -32), an icon has none. Computed from the sprite size so
/// an unusual size still lands on the tile's top vertex correctly.
void apply_canonical_origin(AtlasBinding& binding);

/// Cell size the picker should offer for `kind`: the size the renderer expects for
/// that sort of sprite.
void cell_size_for(std::string_view kind, int& w, int& h);

/// The binding for (kind, id), if the file has one.
std::optional<AtlasBinding> find_binding(const std::string& text,
                                         std::string_view kind,
                                         sim::ItemTypeId id);

/// `text` with the line for (binding.kind, binding.id) replaced, or the line
/// appended when the file has none. Returns text unchanged if the binding is not
/// usable (no kind, id 0, empty size).
std::string upsert_binding(const std::string& text, const AtlasBinding& binding);

/// `text` with the line for (kind, id) removed, if present.
std::string remove_binding(const std::string& text, std::string_view kind,
                           sim::ItemTypeId id);

/// Formats one line the way the file already looks, so a written line is
/// indistinguishable from a hand-written one.
std::string format_binding(const AtlasBinding& binding);

/// One appearance's animated sprite set: the `mobstrip` line.
///
/// The cells sit in one atlas row starting at (x, y), direction-major — cell
/// dir * frames + frame — which is how tools/import_otsp.py lays them down and how
/// Tileset::parse_atlas_meta reads them back. A whole set on one line rather than
/// dirs*frames separate lines because a 4x3 set written longhand is twelve lines that
/// have to agree with each other, and nothing checks that they do.
struct MobStrip {
    std::uint16_t appearance = 0;
    int           x = 0;
    int           y = 0;
    int           cell_w = 32;
    int           cell_h = 32;
    /// 4 for Tibia-style art, 8 for one sprite per grid direction.
    int           dirs = 4;
    int           frames = 3;
    float         origin_x = 0.0F;
    float         origin_y = 0.0F;
    /// Clockwise lean in DEGREES, about the sprite's feet. Last on the line and
    /// optional, so every mobstrip written before leaning existed still parses.
    /// Tibia-style art is drawn for an axis-aligned grid; on an isometric diamond a
    /// creature the artist drew as a diagonal streak needs a few degrees to stand up.
    float         tilt = 0.0F;
};

/// Origin that puts the sprite's feet on the tile centre, whatever the cell size:
/// (-w/2, kHalfTileHeight - h). The mob bands already in atlas.txt follow it —
/// 24x24 is (-12, -8) and 32x48 is (-16, -32) — and getting it wrong is not subtle:
/// the health bar hangs off the sprite's own top edge, so a wrong origin floats the
/// bar as well as the mob.
void apply_canonical_mob_origin(MobStrip& strip);

/// The strip bound to `appearance`, if the file has one.
std::optional<MobStrip> find_mob_strip(const std::string& text,
                                       std::uint16_t appearance);

std::string format_mob_strip(const MobStrip& strip);

/// `text` with `strip`'s line replaced or appended. Any older per-direction `mob`
/// lines for the same appearance are REMOVED: both kinds bind the same appearance and
/// they parse in file order, so leaving them would make which art wins depend on
/// where in the file each line happens to sit.
std::string upsert_mob_strip(const std::string& text, const MobStrip& strip);

}  // namespace editor

#pragma once

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

}  // namespace editor

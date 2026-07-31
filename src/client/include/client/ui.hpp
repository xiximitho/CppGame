#pragma once

#include <string_view>

#include "client/renderer2d.hpp"
#include "client/tileset.hpp"

// Screen-space drawing primitives, shared by the in-game overlays and the
// editor's panels.
//
// Everything here takes WINDOW pixels and converts internally. That conversion is
// the only subtle part: sprites are submitted in world-screen space and then
// transformed by the camera, so a UI element that must not move with the camera
// has to be projected back through window_to_world() and have its size divided by
// the zoom. Getting that wrong makes a panel drift when you pan, which looks like
// a camera bug rather than a UI one.
//
// Text is drawn from the atlas' bitmap font (the `font` line in atlas.txt), which
// means glyphs are just more quads out of the one texture — the whole frame stays
// a single draw call. There is no font file, no shaping and no kerning: cells are
// fixed-width and the pen advances by one cell.

namespace client::ui {

/// Depth every UI quad sorts at, far beyond any world depth key so the overlay is
/// always on top. Add a small offset to layer within the UI itself (panel, then
/// cell, then icon, then label).
constexpr float kDepth = 1.0e7F;

/// Draws one atlas entry at a window-pixel rect. Invalid entries draw nothing.
///
/// `rotation` is clockwise radians about the quad's bottom centre, the same pivot the
/// world uses (SpriteCmd::rotation). It exists so the editor can preview a mob's lean
/// with the transform the game will apply, rather than with an imitation of it.
void sprite(Renderer2D& renderer, const Tileset& tileset, const AtlasEntry& entry,
            float x, float y, float w, float h, Color tint, float depth = kDepth,
            float rotation = 0.0F);

/// Filled rectangle, via the atlas' solid texel so it batches with everything
/// else.
void fill(Renderer2D& renderer, const Tileset& tileset, float x, float y, float w,
          float h, Color tint, float depth = kDepth);

/// Draws `text` with its top-left corner at (x, y). `scale` multiplies the glyph
/// cell, so 2.0 gives chunky double-size text with no resampling. Characters the
/// font lacks advance the pen without drawing, which keeps columns aligned.
void text(Renderer2D& renderer, const Tileset& tileset, std::string_view str,
          float x, float y, Color tint, float scale = 1.0F,
          float depth = kDepth + 4.0F);

/// Width in window pixels that text() would occupy. For right-aligning and for
/// sizing a panel to its longest label.
float text_width(const Tileset& tileset, std::string_view str, float scale = 1.0F);

/// Height in window pixels of one line at `scale`.
float text_height(const Tileset& tileset, float scale = 1.0F);

}  // namespace client::ui

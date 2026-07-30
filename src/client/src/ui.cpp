#include "client/ui.hpp"

namespace client::ui {

void sprite(Renderer2D& renderer, const Tileset& tileset, const AtlasEntry& entry,
            float x, float y, float w, float h, Color tint, float depth) {
    if (!entry.valid) {
        return;
    }
    // Undo the camera so the quad lands at a fixed window position: project the
    // window point into world-screen space and shrink the size by the zoom the
    // renderer is about to apply.
    const float zoom = renderer.camera_zoom();
    float wx = 0.0F;
    float wy = 0.0F;
    renderer.window_to_world(x, y, wx, wy);

    SpriteCmd cmd;
    cmd.texture = tileset.texture();
    cmd.uv = entry.uv;
    cmd.dst = Rect{wx, wy, w / zoom, h / zoom};
    cmd.depth = depth;
    cmd.tint = tint;
    renderer.submit(cmd);
}

void fill(Renderer2D& renderer, const Tileset& tileset, float x, float y, float w,
          float h, Color tint, float depth) {
    sprite(renderer, tileset, tileset.solid(), x, y, w, h, tint, depth);
}

void text(Renderer2D& renderer, const Tileset& tileset, std::string_view str,
          float x, float y, Color tint, float scale, float depth) {
    if (!tileset.has_font()) {
        return;
    }
    const float advance = tileset.glyph_advance() * scale;
    const float height = tileset.glyph_height() * scale;
    float pen = x;
    for (const char c : str) {
        // Drawn even for space (its cell is empty), because branching on the
        // character here would just duplicate what an invalid entry already does.
        sprite(renderer, tileset, tileset.glyph(c), pen, y, advance, height, tint,
               depth);
        pen += advance;
    }
}

float text_width(const Tileset& tileset, std::string_view str, float scale) {
    return static_cast<float>(str.size()) * tileset.glyph_advance() * scale;
}

float text_height(const Tileset& tileset, float scale) {
    return tileset.glyph_height() * scale;
}

}  // namespace client::ui

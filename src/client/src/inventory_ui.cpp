#include "client/inventory_ui.hpp"

#include <cstddef>

namespace client {
namespace {

constexpr float kUi = 1.0e7F;
constexpr float kCell = 40.0F;
constexpr float kPad = 8.0F;
constexpr float kPanelW = 2.0F * kCell + 3.0F * kPad;  // two columns

/// Draws an atlas region at a fixed window rectangle regardless of the camera,
/// by inverting the camera transform (same trick the editor menu uses).
void screen_sprite(Renderer2D& renderer, const Tileset& tileset,
                   const AtlasEntry& entry, float sx, float sy, float sw,
                   float sh, Color tint, float depth) {
    if (!entry.valid) {
        return;
    }
    const float zoom = renderer.camera_zoom();
    float wx = 0.0F;
    float wy = 0.0F;
    renderer.window_to_world(sx, sy, wx, wy);
    SpriteCmd sprite;
    sprite.texture = tileset.texture();
    sprite.uv = entry.uv;
    sprite.dst = Rect{wx, wy, sw / zoom, sh / zoom};
    sprite.depth = depth;
    sprite.tint = tint;
    renderer.submit(sprite);
}

void fill(Renderer2D& renderer, const Tileset& tileset, float sx, float sy,
          float sw, float sh, Color tint, float depth) {
    screen_sprite(renderer, tileset, tileset.solid(), sx, sy, sw, sh, tint,
                  depth);
}

// Draws an item icon fitted (aspect-preserving) inside a cell.
void icon_in_cell(Renderer2D& renderer, const Tileset& tileset, sim::TileId id,
                  float cx, float cy) {
    const AtlasEntry& icon = tileset.icon(id);
    if (!icon.valid) {
        return;
    }
    const float pad = 4.0F;
    const float box = kCell - 2.0F * pad;
    screen_sprite(renderer, tileset, icon, cx + pad, cy + pad, box, box,
                  Color{255, 255, 255, 255}, kUi + 3.0F);
}

}  // namespace

void draw_inventory(Renderer2D& renderer, const Tileset& tileset,
                    const WorldView& view) {
    const auto vw = static_cast<float>(renderer.viewport_width());

    // Equipment: 8 slots in two columns; then the backpack grid below.
    constexpr int kRows = 4;  // 8 equipment slots / 2 columns
    const float panel_h =
        kPad + static_cast<float>(kRows + 3) * (kCell + kPad);
    const float x0 = vw - kPanelW - 16.0F;
    const float y0 = 24.0F;

    fill(renderer, tileset, x0 - kPad, y0 - kPad, kPanelW + 2.0F * kPad,
         panel_h, Color{18, 20, 26, 235}, kUi);

    const auto slot_cell = [&](std::size_t index, float& cx, float& cy) {
        const auto col = static_cast<float>(index % 2);
        const auto row = static_cast<float>(index / 2);
        cx = x0 + kPad + col * (kCell + kPad);
        cy = y0 + kPad + row * (kCell + kPad);
    };

    for (std::size_t i = 0; i < view.equipment.size(); ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        slot_cell(i, cx, cy);
        fill(renderer, tileset, cx, cy, kCell, kCell, Color{44, 47, 55, 255},
             kUi + 1.0F);
        icon_in_cell(renderer, tileset, view.equipment[i], cx, cy);
    }

    // Backpack: a two-column grid starting below the equipment block.
    const float bag_y = y0 + kPad + static_cast<float>(kRows) * (kCell + kPad) +
                        kPad;
    fill(renderer, tileset, x0, bag_y - 4.0F, kPanelW - 2.0F * kPad, 2.0F,
         Color{90, 94, 104, 255}, kUi + 1.0F);  // divider
    for (std::size_t i = 0; i < view.inventory.size(); ++i) {
        const auto col = static_cast<float>(i % 2);
        const auto row = static_cast<float>(i / 2);
        const float cx = x0 + kPad + col * (kCell + kPad);
        const float cy = bag_y + row * (kCell + kPad);
        fill(renderer, tileset, cx, cy, kCell, kCell, Color{36, 38, 45, 255},
             kUi + 1.0F);
        icon_in_cell(renderer, tileset, view.inventory[i].id, cx, cy);
    }
}

}  // namespace client

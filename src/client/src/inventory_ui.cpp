#include "client/inventory_ui.hpp"

#include <cstddef>

#include "client/ui.hpp"

namespace client {
namespace {

constexpr float kUi = ui::kDepth;
constexpr float kCell = 40.0F;
constexpr float kPad = 8.0F;
constexpr float kStep = kCell + kPad;
constexpr float kPanelW = 2.0F * kCell + 3.0F * kPad;  // two columns
constexpr int   kEquipRows = 4;                        // 8 slots / 2 columns

// Shared geometry so draw and hit-test never drift. Everything in window pixels.
struct Layout {
    float x0 = 0.0F;      ///< panel left (inside padding)
    float y0 = 0.0F;      ///< first equipment row top
    float bag_y = 0.0F;   ///< first backpack row top
};

Layout layout(const Renderer2D& renderer) {
    Layout out;
    out.x0 = static_cast<float>(renderer.viewport_width()) - kPanelW - 16.0F +
             kPad;
    out.y0 = 24.0F + kPad;
    out.bag_y = out.y0 + static_cast<float>(kEquipRows) * kStep + kPad;
    return out;
}

void cell_rect(float x0, float base_y, std::size_t index, float& cx, float& cy) {
    cx = x0 + static_cast<float>(index % 2) * kStep;
    cy = base_y + static_cast<float>(index / 2) * kStep;
}

bool inside(float mx, float my, float cx, float cy) {
    return mx >= cx && mx < cx + kCell && my >= cy && my < cy + kCell;
}

void icon_in_cell(Renderer2D& renderer, const Tileset& tileset, sim::TileId id,
                  float cx, float cy) {
    const AtlasEntry& icon = tileset.icon(id);
    if (id == sim::kItemNone || !icon.valid) {
        return;
    }
    const float pad = 4.0F;
    ui::sprite(renderer, tileset, icon, cx + pad, cy + pad,
               kCell - 2.0F * pad, kCell - 2.0F * pad,
               Color{255, 255, 255, 255}, kUi + 3.0F);
}

}  // namespace

void draw_inventory(Renderer2D& renderer, const Tileset& tileset,
                    const WorldView& view) {
    const Layout l = layout(renderer);
    const float panel_h =
        kPad + static_cast<float>(kEquipRows + 3) * kStep;

    ui::fill(renderer, tileset, l.x0 - kPad, l.y0 - kPad, kPanelW, panel_h,
             Color{18, 20, 26, 235}, kUi);

    for (std::size_t i = 0; i < view.equipment.size(); ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        cell_rect(l.x0, l.y0, i, cx, cy);
        ui::fill(renderer, tileset, cx, cy, kCell, kCell,
                 Color{44, 47, 55, 255}, kUi + 1.0F);
        icon_in_cell(renderer, tileset, view.equipment[i], cx, cy);
    }

    ui::fill(renderer, tileset, l.x0, l.bag_y - kPad, kCell + kStep, 2.0F,
             Color{90, 94, 104, 255}, kUi + 1.0F);  // divider

    for (std::size_t i = 0; i < view.inventory.size(); ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        cell_rect(l.x0, l.bag_y, i, cx, cy);
        ui::fill(renderer, tileset, cx, cy, kCell, kCell,
                 Color{36, 38, 45, 255}, kUi + 1.0F);
        icon_in_cell(renderer, tileset, view.inventory[i].id, cx, cy);
    }
}

InventoryAction inventory_hit(const Renderer2D& renderer, const WorldView& view,
                              float mouse_x, float mouse_y) {
    const Layout l = layout(renderer);

    for (std::size_t i = 0; i < view.equipment.size(); ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        cell_rect(l.x0, l.y0, i, cx, cy);
        if (inside(mouse_x, mouse_y, cx, cy) &&
            view.equipment[i] != sim::kItemNone) {
            return InventoryAction{InventoryAction::Kind::Unequip,
                                   sim::kItemNone,
                                   static_cast<sim::EquipSlot>(i)};
        }
    }
    for (std::size_t i = 0; i < view.inventory.size(); ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        cell_rect(l.x0, l.bag_y, i, cx, cy);
        if (inside(mouse_x, mouse_y, cx, cy)) {
            return InventoryAction{InventoryAction::Kind::Equip,
                                   view.inventory[i].id,
                                   sim::EquipSlot::Weapon};
        }
    }
    return InventoryAction{};
}

}  // namespace client

#include "client/inventory_ui.hpp"

#include <cstddef>
#include <cstdio>

#include "client/ui.hpp"
#include "client/ui_theme.hpp"

namespace client {
namespace {

constexpr float kUi = ui::kDepth;
constexpr float kCell = 40.0F;
constexpr float kPad = 8.0F;
constexpr float kStep = kCell + kPad;
constexpr float kPanelW = 2.0F * kCell + 3.0F * kPad;  // two columns
constexpr int   kEquipRows = 4;                        // 8 slots / 2 columns
constexpr int   kBagRows = 3;
constexpr float kTitleH = 16.0F;

struct Layout {
    float x0 = 0.0F;       ///< first cell left
    float y0 = 0.0F;       ///< first equipment cell top
    float bag_y = 0.0F;    ///< first backpack cell top
    float panel_x = 0.0F;  ///< outer panel left
    float panel_y = 0.0F;  ///< outer panel top
    float panel_h = 0.0F;
};

Layout make_layout(const Renderer2D& renderer) {
    Layout out;
    out.panel_x = static_cast<float>(renderer.viewport_width()) - kPanelW -
                  theme::kMargin;
    out.panel_y = theme::kMargin;
    out.x0 = out.panel_x + kPad;
    out.y0 = out.panel_y + kPad + kTitleH;
    out.bag_y = out.y0 + static_cast<float>(kEquipRows) * kStep + kPad + kTitleH;
    out.panel_h = (out.bag_y - out.panel_y) +
                  static_cast<float>(kBagRows) * kStep + kPad;
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

float inventory_panel_bottom(const Renderer2D& renderer) {
    const Layout l = make_layout(renderer);
    return l.panel_y + l.panel_h;
}

float inventory_panel_width() { return kPanelW; }

void draw_inventory(Renderer2D& renderer, const Tileset& tileset,
                    const WorldView& view) {
    const Layout l = make_layout(renderer);

    theme::panel(renderer, tileset, l.panel_x, l.panel_y, kPanelW, l.panel_h, kUi);
    ui::text(renderer, tileset, "EQUIP", l.x0, l.panel_y + kPad - 2.0F,
             theme::kGold, 1.0F, kUi + 5.0F);
    theme::title_rule(renderer, tileset, l.x0, l.y0 - 4.0F, kPanelW - 2.0F * kPad);

    for (std::size_t i = 0; i < view.equipment.size(); ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        cell_rect(l.x0, l.y0, i, cx, cy);
        ui::fill(renderer, tileset, cx, cy, kCell, kCell, theme::kPanelBgSoft,
                 kUi + 1.0F);
        // Subtle gold rim on filled slots.
        if (view.equipment[i] != sim::kItemNone) {
            ui::fill(renderer, tileset, cx, cy, kCell, 1.0F, theme::kBorder,
                     kUi + 2.0F);
            ui::fill(renderer, tileset, cx, cy + kCell - 1.0F, kCell, 1.0F,
                     theme::kBorder, kUi + 2.0F);
            ui::fill(renderer, tileset, cx, cy, 1.0F, kCell, theme::kBorder,
                     kUi + 2.0F);
            ui::fill(renderer, tileset, cx + kCell - 1.0F, cy, 1.0F, kCell,
                     theme::kBorder, kUi + 2.0F);
        }
        icon_in_cell(renderer, tileset, view.equipment[i], cx, cy);
    }

    const float bag_title_y = l.bag_y - kTitleH;
    ui::text(renderer, tileset, "BAG", l.x0, bag_title_y,
             theme::kGold, 1.0F, kUi + 5.0F);
    theme::title_rule(renderer, tileset, l.x0, l.bag_y - 4.0F,
                      kPanelW - 2.0F * kPad);

    for (std::size_t i = 0; i < view.inventory.size(); ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        cell_rect(l.x0, l.bag_y, i, cx, cy);
        ui::fill(renderer, tileset, cx, cy, kCell, kCell, theme::kPanelBgSoft,
                 kUi + 1.0F);
        icon_in_cell(renderer, tileset, view.inventory[i].id, cx, cy);
        if (view.inventory[i].count > 1) {
            char n[8];
            std::snprintf(n, sizeof(n), "%d",
                          static_cast<int>(view.inventory[i].count));
            ui::text(renderer, tileset, n, cx + 2.0F, cy + kCell - 12.0F,
                     theme::kText, 1.0F, kUi + 5.0F);
        }
    }
}

InventoryAction inventory_hit(const Renderer2D& renderer, const WorldView& view,
                              float mouse_x, float mouse_y) {
    const Layout l = make_layout(renderer);

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

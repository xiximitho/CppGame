#include "client/corpse_loot_ui.hpp"

#include <algorithm>
#include <cstddef>

#include "client/status_panel_ui.hpp"
#include "client/ui.hpp"
#include "client/ui_theme.hpp"

namespace client {
namespace {

constexpr float kUi = ui::kDepth;
constexpr float kCell = 40.0F;
constexpr float kPad = 8.0F;
constexpr float kStep = kCell + kPad;
constexpr float kPanelW = 2.0F * kCell + 3.0F * kPad;
constexpr float kTitleH = 18.0F;

struct Layout {
    float x0 = theme::kMargin + kPad;
    float y0 = 0.0F;
    float panel_h = 0.0F;
};

/// Stacks under the STATUS panel instead of starting at the top margin: both are
/// left-anchored, so anchoring this one at the margin drew the loot bag straight
/// over the HP and MP bars. Asking the status panel where it ends — rather than
/// hardcoding a gap — is the same contract the battle list has with the inventory.
Layout layout(const Tileset& tileset, const CorpseView& corpse) {
    Layout out;
    out.y0 = status_panel_bottom(tileset) + theme::kGap + kPad + kTitleH;
    const auto rows =
        static_cast<int>((std::max<std::size_t>(corpse.items.size(), 1U) + 1U) /
                         2U);
    out.panel_h = kPad + kTitleH + static_cast<float>(rows) * kStep + kPad;
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

void draw_corpse_loot(Renderer2D& renderer, const Tileset& tileset,
                      const CorpseView& corpse) {
    const Layout l = layout(tileset, corpse);
    theme::panel(renderer, tileset, l.x0 - kPad, l.y0 - kPad - kTitleH, kPanelW,
                 l.panel_h, kUi);
    ui::text(renderer, tileset, "LOOT", l.x0, l.y0 - kTitleH, theme::kGold, 1.0F,
             kUi + 5.0F);
    theme::title_rule(renderer, tileset, l.x0, l.y0 - 4.0F, kPanelW - 2.0F * kPad);

    for (std::size_t i = 0; i < corpse.items.size(); ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        cell_rect(l.x0, l.y0, i, cx, cy);
        ui::fill(renderer, tileset, cx, cy, kCell, kCell, theme::kPanelBgSoft,
                 kUi + 1.0F);
        icon_in_cell(renderer, tileset, corpse.items[i].id, cx, cy);
    }
}

std::optional<std::size_t> corpse_loot_hit(const Tileset& tileset,
                                           const CorpseView& corpse,
                                           float mouse_x, float mouse_y) {
    const Layout l = layout(tileset, corpse);
    for (std::size_t i = 0; i < corpse.items.size(); ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        cell_rect(l.x0, l.y0, i, cx, cy);
        if (inside(mouse_x, mouse_y, cx, cy)) {
            return i;
        }
    }
    return std::nullopt;
}

bool corpse_loot_panel_contains(const Tileset& tileset,
                                const CorpseView& corpse, float mouse_x,
                                float mouse_y) {
    const Layout l = layout(tileset, corpse);
    const float left = l.x0 - kPad;
    const float top = l.y0 - kPad - kTitleH;
    return mouse_x >= left && mouse_x < left + kPanelW && mouse_y >= top &&
           mouse_y < top + l.panel_h;
}

}  // namespace client

#include "client/battle_list.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "client/inventory_ui.hpp"
#include "client/ui.hpp"
#include "client/ui_theme.hpp"

namespace client {
namespace {

constexpr float kUi = ui::kDepth;
constexpr float kRowH = 32.0F;
constexpr float kPad = 8.0F;
constexpr float kThumb = 24.0F;
/// Rows past this are not drawn. A battle list that grows without bound stops being
/// a list of what matters and becomes a wall of text down the screen.
constexpr std::size_t kMaxRows = 9;

float panel_w() { return inventory_panel_width(); }

float panel_x(const Renderer2D& renderer) {
    return static_cast<float>(renderer.viewport_width()) - panel_w() -
           theme::kMargin;
}

/// Top of row `index`. Shared by draw and hit-test so the two cannot drift — the
/// title block's height comes from the font, so it must not be guessed twice.
float row_top(const Tileset& tileset, float top_y, std::size_t index) {
    const float title_h = ui::text_height(tileset) + kPad + 4.0F;
    return top_y + title_h + kPad + static_cast<float>(index) * kRowH;
}

}  // namespace

void draw_battle_list(Renderer2D& renderer, const Tileset& tileset,
                      const WorldView& view, const sim::MonsterRegistry& monsters,
                      sim::NetId current_target, float top_y) {
    const std::vector<BattleEntry> entries = build_battle_list(view);
    const float x = panel_x(renderer);
    const float w = panel_w();
    const std::size_t rows = std::min(entries.size(), kMaxRows);

    const float title_h = ui::text_height(tileset) + kPad + 4.0F;
    const float panel_h =
        title_h + kPad + static_cast<float>(std::max(rows, std::size_t{1})) * kRowH +
        kPad;

    theme::panel(renderer, tileset, x, top_y, w, panel_h, kUi);

    char title[40];
    std::snprintf(title, sizeof title, "BATTLE  %zu", entries.size());
    ui::text(renderer, tileset, title, x + kPad, top_y + kPad,
             theme::kGold, 1.0F, kUi + 5.0F);
    theme::title_rule(renderer, tileset, x + kPad, top_y + title_h - 2.0F,
                      w - 2.0F * kPad);

    if (entries.empty()) {
        ui::text(renderer, tileset, "none nearby", x + kPad, row_top(tileset, top_y, 0),
                 theme::kTextMute, 1.0F, kUi + 5.0F);
        return;
    }

    for (std::size_t i = 0; i < rows; ++i) {
        const BattleEntry& entry = entries[i];
        const float y = row_top(tileset, top_y, i);
        const bool selected = entry.net_id == current_target;

        ui::fill(renderer, tileset, x + kPad * 0.5F, y, w - kPad, kRowH - 3.0F,
                 selected ? Color{48, 38, 22, 255} : theme::kPanelBgSoft,
                 kUi + 1.0F);
        if (selected) {
            const Color gold = theme::kGoldBright;
            const float rw = w - kPad;
            const float rh = kRowH - 3.0F;
            ui::fill(renderer, tileset, x + kPad * 0.5F, y, rw, 1.0F, gold,
                     kUi + 2.0F);
            ui::fill(renderer, tileset, x + kPad * 0.5F, y + rh - 1.0F, rw, 1.0F,
                     gold, kUi + 2.0F);
            ui::fill(renderer, tileset, x + kPad * 0.5F, y, 1.0F, rh, gold,
                     kUi + 2.0F);
            ui::fill(renderer, tileset, x + kPad * 0.5F + rw - 1.0F, y, 1.0F, rh,
                     gold, kUi + 2.0F);
        }

        const AtlasEntry& sprite =
            tileset.actor(sim::Direction::SouthEast, entry.appearance);
        if (sprite.valid) {
            const float scale =
                kThumb / std::max(sprite.width, sprite.height);
            const float sw = sprite.width * scale;
            const float sh = sprite.height * scale;
            ui::sprite(renderer, tileset, sprite, x + kPad + (kThumb - sw) * 0.5F,
                       y + (kRowH - 3.0F - sh) * 0.5F, sw, sh,
                       Color{255, 255, 255, 255}, kUi + 3.0F,
                       tileset.tilt(entry.appearance));
        }

        const float text_x = x + kPad + kThumb + kPad;
        std::string label;
        for (const sim::MonsterTypeId id : monsters.ids()) {
            if (monsters.get(id).appearance == entry.appearance) {
                label = monsters.get(id).name;
                break;
            }
        }
        if (label.empty()) {
            label = entry.appearance == sim::kAppearancePlayer
                        ? "player"
                        : "creature " + std::to_string(entry.appearance);
        }
        char line[64];
        std::snprintf(line, sizeof line, "%.10s %d", label.c_str(),
                      entry.distance);
        ui::text(renderer, tileset, line, text_x, y + 3.0F, theme::kText, 1.0F,
                 kUi + 5.0F);

        if (entry.max_hp > 0) {
            const float frac =
                std::clamp(static_cast<float>(entry.hp) /
                               static_cast<float>(entry.max_hp),
                           0.0F, 1.0F);
            const float bar_w = w - (text_x - x) - kPad * 2.0F;
            const float bar_y = y + kRowH - 12.0F;
            ui::fill(renderer, tileset, text_x, bar_y, bar_w, 4.0F, theme::kHpTrack,
                     kUi + 3.0F);
            const auto red = static_cast<std::uint8_t>((1.0F - frac) * 180.0F + 60.0F);
            const auto green = static_cast<std::uint8_t>(frac * 160.0F + 50.0F);
            ui::fill(renderer, tileset, text_x, bar_y, bar_w * frac, 4.0F,
                     Color{red, green, 48, 255}, kUi + 4.0F);
        }
    }

    if (entries.size() > rows) {
        char more[32];
        std::snprintf(more, sizeof more, "+%zu more", entries.size() - rows);
        ui::text(renderer, tileset, more, x + kPad,
                 top_y + panel_h - ui::text_height(tileset) - 2.0F,
                 theme::kTextMute, 1.0F, kUi + 5.0F);
    }
}

sim::NetId battle_list_hit(const Renderer2D& renderer, const Tileset& tileset,
                           const WorldView& view, float mouse_x, float mouse_y,
                           float top_y) {
    const std::vector<BattleEntry> entries = build_battle_list(view);
    const float x = panel_x(renderer);
    const float w = panel_w();
    if (mouse_x < x || mouse_x >= x + w) {
        return sim::kInvalidNetId;
    }

    const std::size_t rows = std::min(entries.size(), kMaxRows);
    for (std::size_t i = 0; i < rows; ++i) {
        const float y = row_top(tileset, top_y, i);
        if (mouse_y >= y && mouse_y < y + kRowH - 3.0F) {
            return entries[i].net_id;
        }
    }
    return sim::kInvalidNetId;
}

}  // namespace client

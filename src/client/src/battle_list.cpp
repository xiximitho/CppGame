#include "client/battle_list.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "client/ui.hpp"

namespace client {
namespace {

constexpr float kUi = ui::kDepth;
constexpr float kPanelW = 156.0F;
constexpr float kRowH = 30.0F;
constexpr float kPad = 6.0F;
constexpr float kMargin = 16.0F;
constexpr float kThumb = 24.0F;
/// Rows past this are not drawn. A battle list that grows without bound stops being
/// a list of what matters and becomes a wall of text down the screen.
constexpr std::size_t kMaxRows = 9;

float panel_x(const Renderer2D& renderer) {
    return static_cast<float>(renderer.viewport_width()) - kPanelW - kMargin;
}

/// Top of row `index`. Shared by draw and hit-test so the two cannot drift — the
/// title block's height comes from the font, so it must not be guessed twice.
float row_top(const Tileset& tileset, float top_y, std::size_t index) {
    const float title_h = ui::text_height(tileset) + kPad;
    return top_y + title_h + kPad + static_cast<float>(index) * kRowH;
}

}  // namespace

void draw_battle_list(Renderer2D& renderer, const Tileset& tileset,
                      const WorldView& view, const sim::MonsterRegistry& monsters,
                      sim::NetId current_target, float top_y) {
    const std::vector<BattleEntry> entries = build_battle_list(view);
    const float x = panel_x(renderer);
    const std::size_t rows = std::min(entries.size(), kMaxRows);

    const float title_h = ui::text_height(tileset) + kPad;
    const float panel_h = title_h + kPad + static_cast<float>(rows) * kRowH + kPad;

    ui::fill(renderer, tileset, x, top_y, kPanelW, panel_h,
             Color{18, 20, 26, 235}, kUi);

    char title[40];
    std::snprintf(title, sizeof title, "battle  %zu", entries.size());
    ui::text(renderer, tileset, title, x + kPad, top_y + kPad,
             Color{206, 210, 220, 255}, 1.0F, kUi + 4.0F);

    for (std::size_t i = 0; i < rows; ++i) {
        const BattleEntry& entry = entries[i];
        const float y = row_top(tileset, top_y, i);
        const bool selected = entry.net_id == current_target;

        ui::fill(renderer, tileset, x + kPad * 0.5F, y, kPanelW - kPad, kRowH - 3.0F,
                 selected ? Color{58, 52, 26, 255} : Color{34, 37, 45, 255},
                 kUi + 1.0F);
        if (selected) {
            // A gold frame, the same cue the editor's palette uses for the current
            // brush — one visual language for "this is the thing you picked".
            const Color gold{231, 196, 85, 255};
            const float w = kPanelW - kPad;
            const float h = kRowH - 3.0F;
            ui::fill(renderer, tileset, x + kPad * 0.5F, y, w, 1.0F, gold, kUi + 2.0F);
            ui::fill(renderer, tileset, x + kPad * 0.5F, y + h - 1.0F, w, 1.0F, gold,
                     kUi + 2.0F);
            ui::fill(renderer, tileset, x + kPad * 0.5F, y, 1.0F, h, gold, kUi + 2.0F);
            ui::fill(renderer, tileset, x + kPad * 0.5F + w - 1.0F, y, 1.0F, h, gold,
                     kUi + 2.0F);
        }

        // The creature's own sprite, facing south so the row reads as a portrait.
        const AtlasEntry& sprite =
            tileset.actor(sim::Direction::South, entry.appearance);
        if (sprite.valid) {
            const float scale =
                kThumb / std::max(sprite.width, sprite.height);
            const float w = sprite.width * scale;
            const float h = sprite.height * scale;
            ui::sprite(renderer, tileset, sprite, x + kPad + (kThumb - w) * 0.5F,
                       y + (kRowH - 3.0F - h) * 0.5F, w, h,
                       Color{255, 255, 255, 255}, kUi + 3.0F);
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
        ui::text(renderer, tileset, line, text_x, y + 3.0F,
                 Color{224, 226, 232, 255}, 1.0F, kUi + 4.0F);

        // Health bar under the name, same colour ramp as the one over its head.
        if (entry.max_hp > 0) {
            const float frac =
                std::clamp(static_cast<float>(entry.hp) /
                               static_cast<float>(entry.max_hp),
                           0.0F, 1.0F);
            const float bar_w = kPanelW - (text_x - x) - kPad * 2.0F;
            const float bar_y = y + kRowH - 12.0F;
            ui::fill(renderer, tileset, text_x, bar_y, bar_w, 4.0F,
                     Color{16, 16, 20, 255}, kUi + 3.0F);
            const auto red = static_cast<std::uint8_t>((1.0F - frac) * 210.0F + 30.0F);
            const auto green = static_cast<std::uint8_t>(frac * 190.0F + 40.0F);
            ui::fill(renderer, tileset, text_x, bar_y, bar_w * frac, 4.0F,
                     Color{red, green, 48, 255}, kUi + 4.0F);
        }
    }

    if (entries.size() > rows) {
        char more[32];
        std::snprintf(more, sizeof more, "+%zu more", entries.size() - rows);
        ui::text(renderer, tileset, more, x + kPad,
                 top_y + panel_h - ui::text_height(tileset) - 2.0F,
                 Color{150, 154, 164, 255}, 1.0F, kUi + 4.0F);
    }
}

sim::NetId battle_list_hit(const Renderer2D& renderer, const Tileset& tileset,
                           const WorldView& view, float mouse_x, float mouse_y,
                           float top_y) {
    const std::vector<BattleEntry> entries = build_battle_list(view);
    const float x = panel_x(renderer);
    if (mouse_x < x || mouse_x >= x + kPanelW) {
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

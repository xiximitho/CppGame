#include "client/status_panel_ui.hpp"

#include <algorithm>
#include <cstdio>

#include "client/ui.hpp"
#include "client/ui_theme.hpp"
#include "sim/vocation_type.hpp"

namespace client {
namespace {

constexpr float kUi = ui::kDepth;
constexpr float kPanelW = 168.0F;
constexpr float kPad = 8.0F;
constexpr float kBarH = 8.0F;
constexpr Color kHpFill{190, 55, 50, 255};

}  // namespace

void draw_status_panel(Renderer2D& renderer, const Tileset& tileset,
                       const WorldView& view) {
    const sim::ActorState* local = nullptr;
    for (const sim::ActorState& actor : view.actors) {
        if (actor.net_id == view.local_id) {
            local = &actor;
            break;
        }
    }

    const sim::VocationType& voc = sim::default_vocations().get(view.vocation);
    const char* voc_label =
        voc.id != sim::kVocationNone ? voc.code.c_str() : "---";

    const std::int32_t hp = local != nullptr ? local->hp : 0;
    const std::int32_t max_hp = local != nullptr ? local->max_hp : 0;
    const std::int32_t mana = view.max_mana > 0 ? view.mana : 0;
    const std::int32_t max_mana =
        view.max_mana > 0
            ? view.max_mana
            : (voc.id != sim::kVocationNone ? voc.base_mana : 0);

    const float title_h = ui::text_height(tileset) + 4.0F;
    const float panel_h = kPad + title_h + 4.0F + kPad + kBarH + 14.0F + kBarH +
                          14.0F + kPad;
    const float x0 = theme::kMargin;
    const float y0 = theme::kMargin;

    theme::panel(renderer, tileset, x0, y0, kPanelW, panel_h, kUi);
    ui::text(renderer, tileset, "STATUS", x0 + kPad, y0 + kPad, theme::kGold,
             1.0F, kUi + 5.0F);
    theme::title_rule(renderer, tileset, x0 + kPad, y0 + kPad + title_h,
                      kPanelW - 2.0F * kPad);

    char line[48];
    std::snprintf(line, sizeof(line), "%s", voc_label);
    ui::text(renderer, tileset, line, x0 + kPad, y0 + kPad + title_h + 6.0F,
             theme::kText, 1.0F, kUi + 5.0F);

    const float bar_x = x0 + kPad;
    const float bar_w = kPanelW - 2.0F * kPad;
    float bar_y = y0 + kPad + title_h + 24.0F;

    const float hp_frac =
        max_hp > 0
            ? std::clamp(static_cast<float>(hp) / static_cast<float>(max_hp),
                         0.0F, 1.0F)
            : 0.0F;
    ui::fill(renderer, tileset, bar_x, bar_y, bar_w, kBarH, theme::kHpTrack,
             kUi + 1.0F);
    ui::fill(renderer, tileset, bar_x, bar_y, bar_w * hp_frac, kBarH, kHpFill,
             kUi + 2.0F);
    std::snprintf(line, sizeof(line), "HP %d/%d", static_cast<int>(hp),
                  static_cast<int>(max_hp));
    ui::text(renderer, tileset, line, bar_x, bar_y + kBarH + 2.0F,
             theme::kTextDim, 1.0F, kUi + 5.0F);

    bar_y += kBarH + 16.0F;
    const float mp_frac =
        max_mana > 0
            ? std::clamp(
                  static_cast<float>(mana) / static_cast<float>(max_mana), 0.0F,
                  1.0F)
            : 0.0F;
    ui::fill(renderer, tileset, bar_x, bar_y, bar_w, kBarH, theme::kMpTrack,
             kUi + 1.0F);
    ui::fill(renderer, tileset, bar_x, bar_y, bar_w * mp_frac, kBarH,
             theme::kMpFill, kUi + 2.0F);
    std::snprintf(line, sizeof(line), "MP %d/%d", static_cast<int>(mana),
                  static_cast<int>(max_mana));
    ui::text(renderer, tileset, line, bar_x, bar_y + kBarH + 2.0F,
             theme::kTextDim, 1.0F, kUi + 5.0F);
}

}  // namespace client

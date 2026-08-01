#include "client/spell_hotbar_ui.hpp"

#include <cstdio>

#include "client/ui.hpp"
#include "client/ui_theme.hpp"
#include "sim/vocation_type.hpp"

namespace client {
namespace {

constexpr float kUi = ui::kDepth;
constexpr float kBarW = 220.0F;
constexpr float kBarH = 10.0F;
constexpr float kSlot = 44.0F;
constexpr float kPad = 8.0F;

}  // namespace

void draw_spell_hotbar(Renderer2D& renderer, const Tileset& tileset,
                       const WorldView& view, sim::VocationId vocation_fallback) {
    const sim::VocationId vocation =
        view.vocation != sim::kVocationNone ? view.vocation : vocation_fallback;
    const auto spells = sim::spells_for_vocation(vocation);
    const sim::VocationType& spec = sim::default_vocations().get(vocation);

    const float vw = static_cast<float>(renderer.viewport_width());
    const float vh = static_cast<float>(renderer.viewport_height());
    const float panel_w = kBarW + 2.0F * kPad;
    const float panel_h = kPad + kBarH + kPad + kSlot + kPad + 14.0F;
    const float x0 = (vw - panel_w) * 0.5F;
    const float y0 = vh - panel_h - 16.0F;

    theme::panel(renderer, tileset, x0, y0, panel_w, panel_h, kUi);

    const std::int32_t mana = view.max_mana > 0 ? view.mana : 0;
    const std::int32_t max_mana = view.max_mana > 0 ? view.max_mana
                                                    : (spec.id != sim::kVocationNone
                                                           ? spec.base_mana
                                                           : 0);
    const float fill =
        max_mana > 0
            ? static_cast<float>(mana) / static_cast<float>(max_mana)
            : 0.0F;
    const float bar_x = x0 + kPad;
    const float bar_y = y0 + kPad;
    ui::fill(renderer, tileset, bar_x, bar_y, kBarW, kBarH, theme::kMpTrack,
             kUi + 1.0F);
    ui::fill(renderer, tileset, bar_x, bar_y, kBarW * fill, kBarH, theme::kMpFill,
             kUi + 2.0F);

    char mana_label[32];
    std::snprintf(mana_label, sizeof(mana_label), "MP %d/%d",
                  static_cast<int>(mana), static_cast<int>(max_mana));
    ui::text(renderer, tileset, mana_label, bar_x, bar_y + kBarH + 2.0F,
             theme::kTextDim, 1.0F, kUi + 5.0F);

    const float slot_x = x0 + (panel_w - kSlot) * 0.5F;
    const float slot_y = bar_y + kBarH + 16.0F;
    ui::fill(renderer, tileset, slot_x, slot_y, kSlot, kSlot, theme::kPanelBgSoft,
             kUi + 1.0F);
    ui::fill(renderer, tileset, slot_x, slot_y, kSlot, 1.0F, theme::kBorder,
             kUi + 2.0F);
    ui::fill(renderer, tileset, slot_x, slot_y + kSlot - 1.0F, kSlot, 1.0F,
             theme::kBorder, kUi + 2.0F);
    ui::fill(renderer, tileset, slot_x, slot_y, 1.0F, kSlot, theme::kBorder,
             kUi + 2.0F);
    ui::fill(renderer, tileset, slot_x + kSlot - 1.0F, slot_y, 1.0F, kSlot,
             theme::kBorder, kUi + 2.0F);

    if (!spells.empty()) {
        const sim::SpellDef& spell = spells[0];
        const Color tint = spell.kind == sim::SpellKind::Heal
                               ? (spell.effect == sim::kEffectHoly
                                      ? theme::kGoldBright
                                      : Color{90, 200, 110, 255})
                               : (spell.effect == sim::kEffectFirebolt
                                      ? Color{230, 120, 50, 255}
                                      : theme::kTextDim);
        ui::fill(renderer, tileset, slot_x + 4.0F, slot_y + 4.0F, kSlot - 8.0F,
                 kSlot - 8.0F, tint, kUi + 2.0F);
        ui::text(renderer, tileset, "1", slot_x + 4.0F, slot_y + 4.0F,
                 theme::kText, 1.0F, kUi + 5.0F);
        ui::text(renderer, tileset, spell.name, slot_x + kSlot + 6.0F,
                 slot_y + 14.0F, theme::kText, 1.0F, kUi + 5.0F);
    } else {
        ui::text(renderer, tileset, "1 -", slot_x + 8.0F, slot_y + 14.0F,
                 theme::kTextMute, 1.0F, kUi + 5.0F);
    }

    if (spec.id != sim::kVocationNone) {
        ui::text(renderer, tileset, spec.code, x0 + kPad, y0 + panel_h - 14.0F,
                 theme::kGold, 1.0F, kUi + 5.0F);
    }
}

}  // namespace client

#include "client/vocation_select_ui.hpp"

#include <cstdio>

#include <SDL3/SDL_scancode.h>

#include "client/ui.hpp"
#include "client/ui_theme.hpp"
#include "sim/spell.hpp"

namespace client {
namespace {

constexpr float kUi = ui::kDepth;
constexpr float kCardW = 240.0F;
constexpr float kCardH = 108.0F;
constexpr float kGap = 12.0F;
constexpr float kEnterW = 200.0F;
constexpr float kEnterH = 36.0F;
constexpr float kSwatch = 14.0F;
constexpr float kSwatchGap = 3.0F;
constexpr float kPreviewScale = 2.0F;

struct CardGeom {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
};

struct SwatchGeom {
    float x = 0.0F;
    float y = 0.0F;
};

struct Layout {
    float      title_x = 0.0F;
    float      title_y = 0.0F;
    float      sub_y = 0.0F;
    CardGeom   cards[kPlayableVocationCount]{};
    float      outfit_x = 0.0F;
    float      outfit_y = 0.0F;
    float      outfit_w = 0.0F;
    float      preview_x = 0.0F;
    float      preview_y = 0.0F;
    SwatchGeom swatches[sim::kOutfitLayerCount][sim::kOutfitPaletteSize]{};
    float      enter_x = 0.0F;
    float      enter_y = 0.0F;
    float      detail_x = 0.0F;
    float      detail_y = 0.0F;
};

Layout make_layout(float vw, float vh, const Tileset* tileset) {
    Layout layout;
    const float grid_w = kCardW * 2.0F + kGap;
    const float grid_h = kCardH * 2.0F + kGap;
    const float grid_x = (vw - grid_w) * 0.5F;
    const float grid_y = vh * 0.12F;

    layout.title_y = grid_y - 48.0F;
    layout.sub_y = grid_y - 26.0F;
    if (tileset != nullptr) {
        const char* title = "Choose your vocation";
        layout.title_x =
            (vw - ui::text_width(*tileset, title, 2.0F)) * 0.5F;
    }

    for (int i = 0; i < kPlayableVocationCount; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        layout.cards[i].x = grid_x + static_cast<float>(col) * (kCardW + kGap);
        layout.cards[i].y = grid_y + static_cast<float>(row) * (kCardH + kGap);
        layout.cards[i].w = kCardW;
        layout.cards[i].h = kCardH;
    }

    const float palette_w =
        static_cast<float>(sim::kOutfitPaletteSize) * (kSwatch + kSwatchGap) -
        kSwatchGap;
    constexpr float kLayerRowH = 22.0F;
    layout.outfit_w = 56.0F + palette_w + 80.0F;
    layout.outfit_x = (vw - layout.outfit_w) * 0.5F;
    layout.outfit_y = grid_y + grid_h + 14.0F;

    layout.preview_x = layout.outfit_x + 56.0F + palette_w + 16.0F;
    layout.preview_y = layout.outfit_y + 8.0F;

    for (int layer = 0; layer < sim::kOutfitLayerCount; ++layer) {
        // Display order hair→boots (top to bottom); layer enum is feet→head.
        const int display = sim::kOutfitLayerCount - 1 - layer;
        const float row_y =
            layout.outfit_y + static_cast<float>(display) * kLayerRowH;
        for (int c = 0; c < static_cast<int>(sim::kOutfitPaletteSize); ++c) {
            layout.swatches[layer][c].x =
                layout.outfit_x + 56.0F +
                static_cast<float>(c) * (kSwatch + kSwatchGap);
            layout.swatches[layer][c].y = row_y;
        }
    }

    const float outfit_h =
        static_cast<float>(sim::kOutfitLayerCount) * kLayerRowH + 8.0F;
    layout.enter_x = (vw - kEnterW) * 0.5F;
    layout.enter_y = layout.outfit_y + outfit_h + 12.0F;
    layout.detail_x = grid_x;
    layout.detail_y = layout.enter_y + kEnterH + 12.0F;
    return layout;
}

bool point_in(float x0, float y0, float w, float h, float x, float y) {
    return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
}

Color palette_color(std::uint8_t index) {
    const sim::OutfitColor c = sim::outfit_color(index);
    return Color{c.r, c.g, c.b, 255};
}

const char* role_for(sim::VocationId id) {
    switch (id) {
        case sim::vocations::kKnight:
            return "TANK / MELEE";
        case sim::vocations::kPaladin:
            return "HYBRID / RANGED";
        case sim::vocations::kMage:
            return "BURST / CASTER";
        case sim::vocations::kDruid:
            return "HEAL / CONTROL";
        default:
            return "";
    }
}

const char* blurb_for(sim::VocationId id) {
    switch (id) {
        case sim::vocations::kKnight:
            return "Holds the line. High HP, low damage.";
        case sim::vocations::kPaladin:
            return "Arrow and faith. Self-sustain at range.";
        case sim::vocations::kMage:
            return "Burns rooms. Dies in three hits.";
        case sim::vocations::kDruid:
            return "Heals and roots. Needed in deep hunts.";
        default:
            return "";
    }
}

void draw_outfit_preview(Renderer2D& renderer, const Tileset& tileset,
                         const sim::COutfit& outfit, float apex_x, float apex_y) {
    if (!tileset.has_outfit_layers()) {
        return;
    }
    // Base composite under tinted cloth (same stack as world_render).
    const AtlasEntry& base = tileset.actor(sim::Direction::South, 0, 0);
    if (base.valid) {
        ui::sprite(renderer, tileset, base,
                   apex_x + base.origin_x * kPreviewScale,
                   apex_y + base.origin_y * kPreviewScale,
                   base.width * kPreviewScale, base.height * kPreviewScale,
                   Color{255, 255, 255, 255}, kUi + 5.0F);
    }
    constexpr sim::OutfitLayer kOrder[] = {
        sim::OutfitLayer::Feet,
        sim::OutfitLayer::Legs,
        sim::OutfitLayer::Body,
        sim::OutfitLayer::Head,
    };
    for (const sim::OutfitLayer layer : kOrder) {
        const AtlasEntry& entry = tileset.outfit_layer(layer);
        if (!entry.valid) {
            continue;
        }
        const Color tint = palette_color(outfit.index(layer));
        const float dw = entry.width * kPreviewScale;
        const float dh = entry.height * kPreviewScale;
        ui::sprite(renderer, tileset, entry,
                   apex_x + entry.origin_x * kPreviewScale,
                   apex_y + entry.origin_y * kPreviewScale, dw, dh, tint,
                   kUi + 6.0F);
    }
}

}  // namespace

int vocation_picker_index(sim::VocationId id) {
    for (int i = 0; i < kPlayableVocationCount; ++i) {
        if (kPlayableVocations[i] == id) {
            return i;
        }
    }
    return 0;
}

void draw_vocation_picker(Renderer2D& renderer, const Tileset& tileset,
                          const VocationPicker& picker) {
    const float vw = static_cast<float>(renderer.viewport_width());
    const float vh = static_cast<float>(renderer.viewport_height());
    const Layout layout = make_layout(vw, vh, &tileset);

    ui::fill(renderer, tileset, 0.0F, 0.0F, vw, vh, theme::kWorldClear, kUi);

    ui::text(renderer, tileset, "Choose your vocation", layout.title_x,
             layout.title_y, theme::kText, 2.0F, kUi + 5.0F);

    const char* sub = "Pick a class, then hair / armor / boots.";
    const float sub_x = (vw - ui::text_width(tileset, sub, 1.0F)) * 0.5F;
    ui::text(renderer, tileset, sub, sub_x, layout.sub_y, theme::kTextDim, 1.0F,
             kUi + 5.0F);

    for (int i = 0; i < kPlayableVocationCount; ++i) {
        const sim::VocationId id = kPlayableVocations[i];
        const sim::VocationType& spec = sim::default_vocations().get(id);
        const CardGeom& card = layout.cards[i];
        const bool selected = (i == picker.selected);

        theme::panel(renderer, tileset, card.x, card.y, card.w, card.h,
                     kUi + 1.0F);
        if (selected) {
            ui::fill(renderer, tileset, card.x, card.y, card.w, 2.0F,
                     theme::kGold, kUi + 3.0F);
            ui::fill(renderer, tileset, card.x, card.y + card.h - 2.0F, card.w,
                     2.0F, theme::kGold, kUi + 3.0F);
            ui::fill(renderer, tileset, card.x, card.y, 2.0F, card.h,
                     theme::kGold, kUi + 3.0F);
            ui::fill(renderer, tileset, card.x + card.w - 2.0F, card.y, 2.0F,
                     card.h, theme::kGold, kUi + 3.0F);
        }

        char key[4];
        std::snprintf(key, sizeof(key), "%d", i + 1);
        ui::text(renderer, tileset, key, card.x + 10.0F, card.y + 8.0F,
                 theme::kGold, 1.0F, kUi + 5.0F);
        ui::text(renderer, tileset, spec.code, card.x + 28.0F, card.y + 8.0F,
                 theme::kGold, 1.0F, kUi + 5.0F);
        ui::text(renderer, tileset, spec.name, card.x + 10.0F, card.y + 26.0F,
                 theme::kText, 1.5F, kUi + 5.0F);
        ui::text(renderer, tileset, role_for(id), card.x + 10.0F, card.y + 50.0F,
                 theme::kGoldBright, 1.0F, kUi + 5.0F);

        const auto spells = sim::spells_for_vocation(id);
        char skill[48];
        if (!spells.empty()) {
            std::snprintf(skill, sizeof(skill), "skill: %.*s",
                          static_cast<int>(spells[0].name.size()),
                          spells[0].name.data());
        } else {
            std::snprintf(skill, sizeof(skill), "skill: -");
        }
        ui::text(renderer, tileset, skill, card.x + 10.0F, card.y + 70.0F,
                 theme::kTextDim, 1.0F, kUi + 5.0F);

        char stats[64];
        std::snprintf(stats, sizeof(stats), "HP %d  MP %d",
                      static_cast<int>(spec.base_hp),
                      static_cast<int>(spec.base_mana));
        ui::text(renderer, tileset, stats, card.x + 10.0F, card.y + 88.0F,
                 theme::kTextMute, 1.0F, kUi + 5.0F);
    }

    // Outfit rows + preview.
    constexpr sim::OutfitLayer kLayers[] = {
        sim::OutfitLayer::Head,
        sim::OutfitLayer::Body,
        sim::OutfitLayer::Legs,
        sim::OutfitLayer::Feet,
    };
    for (const sim::OutfitLayer layer : kLayers) {
        const int li = static_cast<int>(layer);
        ui::text(renderer, tileset, sim::outfit_layer_label(layer),
                 layout.outfit_x, layout.swatches[li][0].y, theme::kTextMute,
                 1.0F, kUi + 5.0F);
        for (int c = 0; c < static_cast<int>(sim::kOutfitPaletteSize); ++c) {
            const SwatchGeom& s = layout.swatches[li][c];
            const Color fill = palette_color(static_cast<std::uint8_t>(c));
            ui::fill(renderer, tileset, s.x, s.y, kSwatch, kSwatch, fill,
                     kUi + 2.0F);
            if (picker.outfit.index(layer) == static_cast<std::uint8_t>(c)) {
                ui::fill(renderer, tileset, s.x - 1.0F, s.y - 1.0F,
                         kSwatch + 2.0F, 1.0F, theme::kGoldBright, kUi + 4.0F);
                ui::fill(renderer, tileset, s.x - 1.0F, s.y + kSwatch,
                         kSwatch + 2.0F, 1.0F, theme::kGoldBright, kUi + 4.0F);
                ui::fill(renderer, tileset, s.x - 1.0F, s.y - 1.0F, 1.0F,
                         kSwatch + 2.0F, theme::kGoldBright, kUi + 4.0F);
                ui::fill(renderer, tileset, s.x + kSwatch, s.y - 1.0F, 1.0F,
                         kSwatch + 2.0F, theme::kGoldBright, kUi + 4.0F);
            }
        }
    }
    draw_outfit_preview(renderer, tileset, picker.outfit, layout.preview_x,
                        layout.preview_y);

    ui::fill(renderer, tileset, layout.enter_x, layout.enter_y, kEnterW, kEnterH,
             theme::kGold, kUi + 1.0F);
    const char* enter = "ENTER";
    const float enter_tx =
        layout.enter_x + (kEnterW - ui::text_width(tileset, enter, 1.5F)) * 0.5F;
    const float enter_ty =
        layout.enter_y + (kEnterH - ui::text_height(tileset, 1.5F)) * 0.5F;
    ui::text(renderer, tileset, enter, enter_tx, enter_ty,
             Color{23, 19, 10, 255}, 1.5F, kUi + 5.0F);

    const sim::VocationId chosen = vocation_picker_id(picker);
    const sim::VocationType& chosen_spec =
        sim::default_vocations().get(chosen);
    char detail[96];
    std::snprintf(detail, sizeof(detail), "selected: %s  -  %s",
                  chosen_spec.name.c_str(), blurb_for(chosen));
    ui::text(renderer, tileset, detail, layout.detail_x, layout.detail_y,
             theme::kTextDim, 1.0F, kUi + 5.0F);

    ui::text(renderer, tileset, "1-4 select   click colours   Enter confirm",
             layout.detail_x, layout.detail_y + 16.0F, theme::kTextMute, 1.0F,
             kUi + 5.0F);
}

bool vocation_picker_click(const Renderer2D& renderer, float mouse_x,
                           float mouse_y, VocationPicker& picker) {
    const float vw = static_cast<float>(renderer.viewport_width());
    const float vh = static_cast<float>(renderer.viewport_height());
    const Layout layout = make_layout(vw, vh, nullptr);

    for (int layer = 0; layer < sim::kOutfitLayerCount; ++layer) {
        for (int c = 0; c < static_cast<int>(sim::kOutfitPaletteSize); ++c) {
            const SwatchGeom& s = layout.swatches[layer][c];
            if (point_in(s.x, s.y, kSwatch, kSwatch, mouse_x, mouse_y)) {
                picker.outfit.set(static_cast<sim::OutfitLayer>(layer),
                                  static_cast<std::uint8_t>(c));
                return true;
            }
        }
    }

    for (int i = 0; i < kPlayableVocationCount; ++i) {
        const CardGeom& card = layout.cards[i];
        if (point_in(card.x, card.y, card.w, card.h, mouse_x, mouse_y)) {
            picker.selected = i;
            return true;
        }
    }
    if (point_in(layout.enter_x, layout.enter_y, kEnterW, kEnterH, mouse_x,
                 mouse_y)) {
        picker.confirmed = true;
        return true;
    }
    return false;
}

bool vocation_picker_key(int scancode, VocationPicker& picker) {
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_4) {
        picker.selected = scancode - SDL_SCANCODE_1;
        return true;
    }
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_4) {
        picker.selected = scancode - SDL_SCANCODE_KP_1;
        return true;
    }
    if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER ||
        scancode == SDL_SCANCODE_SPACE) {
        picker.confirmed = true;
        return true;
    }
    return false;
}

}  // namespace client

#pragma once

#include "client/renderer2d.hpp"
#include "client/tileset.hpp"
#include "client/ui.hpp"

// Grimhold HUD palette — warm parchment / gold, from the claude_design mockup.
// Panels share one look so inventory, battle list and hotbar stop reading as
// three different UIs stacked by accident.

namespace client::theme {

inline constexpr Color kPanelBg{26, 23, 19, 240};       // #1a1713
inline constexpr Color kPanelBgSoft{36, 30, 22, 255};   // slot well
inline constexpr Color kBorder{46, 39, 32, 255};        // #2e2720
inline constexpr Color kGold{201, 162, 39, 255};        // #c9a227
inline constexpr Color kGoldBright{231, 196, 85, 255};  // #e7c455
inline constexpr Color kText{230, 223, 208, 255};       // #e6dfd0
inline constexpr Color kTextDim{154, 144, 124, 255};    // #9a907c
inline constexpr Color kTextMute{111, 102, 89, 255};    // #6f6659
inline constexpr Color kHpTrack{42, 15, 13, 255};       // #2a0f0d
inline constexpr Color kMpTrack{14, 26, 42, 255};       // #0e1a2a
inline constexpr Color kMpFill{70, 120, 200, 255};
inline constexpr Color kWorldClear{20, 18, 14, 255};    // warmer than cold grey

inline constexpr float kMargin = 14.0F;
inline constexpr float kGap = 10.0F;

/// Filled panel with a 1px border. `depth` is the panel body; border is +1.
inline void panel(Renderer2D& renderer, const Tileset& tileset, float x, float y,
                  float w, float h, float depth = ui::kDepth) {
    ui::fill(renderer, tileset, x, y, w, h, kPanelBg, depth);
    ui::fill(renderer, tileset, x, y, w, 1.0F, kBorder, depth + 1.0F);
    ui::fill(renderer, tileset, x, y + h - 1.0F, w, 1.0F, kBorder, depth + 1.0F);
    ui::fill(renderer, tileset, x, y, 1.0F, h, kBorder, depth + 1.0F);
    ui::fill(renderer, tileset, x + w - 1.0F, y, 1.0F, h, kBorder, depth + 1.0F);
}

/// Gold underline under a section title (mockup accent).
inline void title_rule(Renderer2D& renderer, const Tileset& tileset, float x,
                       float y, float w, float depth = ui::kDepth + 2.0F) {
    ui::fill(renderer, tileset, x, y, w, 1.0F, kGold, depth);
}

}  // namespace client::theme

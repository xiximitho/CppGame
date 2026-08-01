#pragma once

#include "client/renderer2d.hpp"
#include "client/tileset.hpp"
#include "sim/outfit.hpp"
#include "sim/vocation_type.hpp"

// Pre-game vocation + outfit picker. Blocks until the player confirms a
// playable class (CAV/PAL/MAG/DRU) or quits. Screenshot / --vocation skip this
// in main (outfit then keeps COutfit defaults).

namespace client {

/// Playable classes shown on the picker (stubs stay off the board).
inline constexpr sim::VocationId kPlayableVocations[] = {
    sim::vocations::kKnight,
    sim::vocations::kPaladin,
    sim::vocations::kMage,
    sim::vocations::kDruid,
};
inline constexpr int kPlayableVocationCount = 4;

inline bool is_playable_vocation(sim::VocationId id) {
    for (const sim::VocationId v : kPlayableVocations) {
        if (v == id) {
            return true;
        }
    }
    return false;
}

struct VocationPicker {
    int          selected = 0;  ///< index into kPlayableVocations
    bool         confirmed = false;
    sim::COutfit outfit{};
};

/// Full-screen picker. Call each frame between begin_frame / end_frame.
void draw_vocation_picker(Renderer2D& renderer, const Tileset& tileset,
                          const VocationPicker& picker);

/// Hit-test a click. Updates selection / outfit / confirmed. Returns true when
/// the click landed on something.
bool vocation_picker_click(const Renderer2D& renderer, float mouse_x,
                           float mouse_y, VocationPicker& picker);

/// Keyboard: 1–4 select, Enter/Return confirm. Returns true if handled.
bool vocation_picker_key(int scancode, VocationPicker& picker);

/// Convenience: index for a vocation id, or 0 if unknown.
int vocation_picker_index(sim::VocationId id);

inline sim::VocationId vocation_picker_id(const VocationPicker& picker) {
    if (picker.selected < 0 || picker.selected >= kPlayableVocationCount) {
        return sim::vocations::kKnight;
    }
    return kPlayableVocations[picker.selected];
}

}  // namespace client

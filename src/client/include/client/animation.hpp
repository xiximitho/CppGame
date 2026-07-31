#pragma once

#include <cstdint>

#include "sim/types.hpp"

// Which sprite frame an actor is drawn with.
//
// This is presentation and nothing else: the whole file is a pure function of what a
// snapshot already carries (`walking`, `walk_progress`, `facing`), so animating cost
// no protocol change, no server change and nothing in sim/. That is also what makes
// it robust — the phase is DERIVED from the step's progress instead of accumulated in
// a local counter, so a dropped snapshot cannot desync the animation any more than it
// can desync the position (see the netcode note in CLAUDE.md).
//
// Header-only and free-standing on purpose, like client/battle_list.hpp: the choice
// of frame is the part worth testing, and it must be testable without a window.

namespace client::anim {

/// Most frames one direction of a sprite set may have. Tibia art ships 3; the cap
/// exists only to keep MobSprites a fixed-size array.
constexpr int kMaxFrames = 8;

/// How many art directions a sprite set may declare.
///
/// 8 is one column per grid direction. 4 is what Tibia-style art actually has, in
/// the order the sheets are drawn in: 0 back (away from the camera), 1 right,
/// 2 front, 3 left.
constexpr int kArtDirsFull = 8;
constexpr int kArtDirsTibia = 4;

/// Sprite column for a grid facing, in a set that has `dirs` of them.
///
/// The interesting case is 4. `sim::Direction` is in GRID space, and in the 2:1
/// projection the grid DIAGONALS are what point along the screen axes: NorthWest is
/// screen-up, NorthEast screen-right, SouthEast screen-down, SouthWest screen-left
/// (`tests/test_input.cpp` derives that from the projection itself). So the four art
/// directions are the four grid diagonals, and each grid cardinal sits exactly
/// halfway between two of them — a tie, broken clockwise. The result is two grid
/// directions per art direction, and it reads correctly: walking screen-up or
/// up-left shows the back, walking screen-down or down-right shows the front.
///
/// Anything other than 4 is treated as a full 8-direction set, because that is what
/// every atlas written before animation existed is.
constexpr std::uint8_t art_direction(sim::Direction facing, int dirs) {
    const auto grid = static_cast<int>(facing);
    if (dirs != kArtDirsTibia) {
        return static_cast<std::uint8_t>(grid);
    }
    // North->right, NorthEast->right, East->front, SouthEast->front, South->left,
    // SouthWest->left, West->back, NorthWest->back.
    return static_cast<std::uint8_t>(((grid + 2) / 2) % kArtDirsTibia);
}

/// Frame of the walk cycle to draw.
///
/// Standing still is frame 0 and walking runs the whole cycle across the step. The
/// alternative — reserving frame 0 as a rest pose and cycling only the rest — is
/// wrong for this art: the three frames of an OTSP creature are a loop of equal
/// poses, not a neutral plus two swings, so holding one of them back would drop a
/// third of the animation.
constexpr std::uint8_t walk_frame(bool walking, std::uint8_t progress,
                                  std::uint8_t frames) {
    if (frames <= 1 || !walking) {
        return 0;
    }
    // progress is 0..255, so this never reaches `frames`.
    return static_cast<std::uint8_t>((static_cast<int>(progress) *
                                      static_cast<int>(frames)) /
                                     256);
}

}  // namespace client::anim

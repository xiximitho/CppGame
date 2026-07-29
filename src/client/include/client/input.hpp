#pragma once

#include "sim/types.hpp"

// Deliberately no SDL include here: the pure mapping functions below are the
// interesting part to test, and tests should not have to link a window system to
// check that pressing W walks the right way.

namespace client::input {

/// How movement keys relate to the isometric grid.
///
/// This is a real design decision, not a toggle to avoid making one. On a 2:1
/// projection the grid axes appear as screen diagonals, so:
///
///   ScreenRelative — W moves visually up. Intuitive, but "up" is grid NorthWest,
///                    a diagonal step, which costs 1.5x under the current speed
///                    rule. Flatten the diagonal cost if this scheme is kept.
///   GridAligned    — W moves grid North, appearing as up-and-right on screen.
///                    Matches the tile grid and all four keys cost the same, at
///                    the price of looking rotated to new players.
///
/// Both are implemented so the choice can be made by feel rather than by whichever
/// one happened to be written first. F2 toggles at runtime.
enum class Scheme {
    ScreenRelative,
    GridAligned,
};

/// Player intent in screen terms, before the isometric rotation is applied.
enum class ScreenDir : int {
    Up = 0,
    UpRight,
    Right,
    DownRight,
    Down,
    DownLeft,
    Left,
    UpLeft,
    None,
};

/// Applies the isometric rotation for the given scheme.
constexpr sim::Direction to_grid(ScreenDir dir, Scheme scheme) {
    if (dir == ScreenDir::None) {
        return sim::Direction::South;
    }
    const int index = static_cast<int>(dir);

    // GridAligned is the identity mapping: ScreenDir::Up == Direction::North.
    // ScreenRelative rotates by one eighth turn, because grid NorthWest is what
    // projects to straight up on a 2:1 isometric screen.
    const int rotation = (scheme == Scheme::ScreenRelative) ? 7 : 0;
    return static_cast<sim::Direction>((index + rotation) % sim::kDirectionCount);
}

/// The single step that moves closest to `to`. Used for click-to-move and touch,
/// where the player names a destination rather than a direction.
///
/// Not a pathfinder: it walks straight into walls. Real click-to-move needs A* over
/// the tile grid, which is the next thing to build here.
constexpr sim::Direction direction_towards(sim::TilePos from, sim::TilePos to) {
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;

    const int sx = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
    const int sy = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);

    for (int i = 0; i < sim::kDirectionCount; ++i) {
        const auto candidate = static_cast<sim::Direction>(i);
        const sim::TileDelta delta = sim::direction_delta(candidate);
        if (delta.dx == sx && delta.dy == sy) {
            return candidate;
        }
    }
    // Only reachable when from == to, where standing still is the right answer.
    return sim::Direction::South;
}

/// Current keyboard state as a single intent. WASD and the arrow keys, with
/// opposing keys cancelling out. ScreenDir::None when nothing relevant is held.
/// Defined in input.cpp because it is the one part that needs SDL.
ScreenDir held_direction();

}  // namespace client::input

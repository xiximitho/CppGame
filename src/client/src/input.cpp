#include "client/input.hpp"

#include <SDL3/SDL.h>

namespace client::input {

ScreenDir held_direction() {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr) {
        return ScreenDir::None;
    }

    const bool up = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    const bool down = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
    const bool left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    const bool right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];

    // Opposing keys cancel, so rolling from one direction to its opposite never
    // produces a spurious diagonal on the way through.
    const bool vertical = up != down;
    const bool horizontal = left != right;

    if (vertical && horizontal) {
        if (up) {
            return left ? ScreenDir::UpLeft : ScreenDir::UpRight;
        }
        return left ? ScreenDir::DownLeft : ScreenDir::DownRight;
    }
    if (vertical) {
        return up ? ScreenDir::Up : ScreenDir::Down;
    }
    if (horizontal) {
        return left ? ScreenDir::Left : ScreenDir::Right;
    }
    return ScreenDir::None;
}

}  // namespace client::input

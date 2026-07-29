#pragma once

#include <memory>

#include "client/renderer2d.hpp"

// Forward declared so including this header does not pull in SDL.
struct SDL_Renderer;

namespace client {

/// Renderer2D on top of SDL_Render.
///
/// Sprites are accumulated, sorted by depth and emitted through
/// SDL_RenderGeometry in runs that share a texture. With a single atlas that is
/// one draw call for the whole scene; check last_draw_calls() if that stops
/// being true after adding art.
std::unique_ptr<Renderer2D> make_sdl_renderer(SDL_Renderer* sdl_renderer);

}  // namespace client

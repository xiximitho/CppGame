#pragma once

#include "client/renderer2d.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"

namespace client {

/// Top-left STATUS panel: vocation code, HP and MP bars (Grimhold theme).
void draw_status_panel(Renderer2D& renderer, const Tileset& tileset,
                       const WorldView& view);

}  // namespace client

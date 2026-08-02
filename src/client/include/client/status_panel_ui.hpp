#pragma once

#include "client/renderer2d.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"

namespace client {

/// Top-left STATUS panel: vocation code, HP and MP bars (Grimhold theme).
void draw_status_panel(Renderer2D& renderer, const Tileset& tileset,
                       const WorldView& view);

/// Where this panel ends on screen, so a panel below it can stack without
/// knowing what it contains — same contract as inventory_panel_bottom(). The
/// height depends on the font, hence the tileset.
float status_panel_bottom(const Tileset& tileset);

}  // namespace client

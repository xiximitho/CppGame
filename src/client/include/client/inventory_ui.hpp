#pragma once

#include "client/renderer2d.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"

namespace client {

/// Draws the inventory/equipment panel for the local player from the world view
/// (equipment slots on top, backpack below), as a fixed-size overlay. Call
/// between begin_frame and end_frame when the panel is toggled on.
void draw_inventory(Renderer2D& renderer, const Tileset& tileset,
                    const WorldView& view);

}  // namespace client

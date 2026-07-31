#pragma once

#include "client/renderer2d.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"
#include "sim/types.hpp"

namespace client {

struct RenderParams {
    /// Tile under the cursor, highlighted when valid.
    sim::TilePos hover;
    bool         hover_valid = false;

    /// Floor the view is centred on, or -1 to use the local actor's.
    ///
    /// Exists for the editor, which has no actor and edits one floor at a time:
    /// without it every tool that draws the world would be stuck on floor 0, which
    /// is exactly why a stair could be painted but never authored end to end. Set,
    /// it also caps what is drawn — the floors above are the thing you are trying
    /// to see under.
    int floor_override = -1;
};

/// Where the camera should sit to follow the local actor, in world-screen pixels.
/// Returns false when the local actor is not in view yet (still connecting).
bool camera_target(const WorldView& view, float& out_x, float& out_y);

/// Which floor the local actor is on. Falls back to 0 when unknown.
int local_floor(const WorldView& view);

/// Submits the whole scene. Does not clear or present — the caller brackets this
/// with begin_frame/end_frame so a HUD can be layered on top later.
void render_world(Renderer2D& renderer, const Tileset& tileset,
                  const WorldView& view, const RenderParams& params);

}  // namespace client

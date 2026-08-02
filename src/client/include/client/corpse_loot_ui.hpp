#pragma once

#include <cstddef>
#include <optional>

#include "client/renderer2d.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"
#include "sim/types.hpp"

namespace client {

/// Left-side panel showing a phantom corpse's inventory. Same cell language as
/// the player inventory so clicks feel familiar.
void draw_corpse_loot(Renderer2D& renderer, const Tileset& tileset,
                      const CorpseView& corpse);

/// Hit-test against draw_corpse_loot. Returns the backpack index clicked, or
/// nullopt when the click missed every cell.
/// Takes the tileset, not the renderer: the panel's position depends on where the
/// STATUS panel above it ends, and that depends on the font.
std::optional<std::size_t> corpse_loot_hit(const Tileset& tileset,
                                           const CorpseView& corpse,
                                           float mouse_x, float mouse_y);

/// True when the point lies on the loot panel rectangle (so a miss on a cell
/// still does not fall through to the world).
bool corpse_loot_panel_contains(const Tileset& tileset,
                                const CorpseView& corpse, float mouse_x,
                                float mouse_y);

}  // namespace client

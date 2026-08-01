#pragma once

#include "client/renderer2d.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"
#include "sim/spell.hpp"
#include "sim/vocation_type.hpp"

namespace client {

/// Bottom-centre hotbar: mana bar + one spell slot (key `1`). Uses `vocation`
/// when the view has none yet (remote until mana is on the wire).
void draw_spell_hotbar(Renderer2D& renderer, const Tileset& tileset,
                       const WorldView& view, sim::VocationId vocation_fallback);

}  // namespace client

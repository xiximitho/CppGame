#pragma once

#include "client/renderer2d.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"
#include "sim/item_type.hpp"
#include "sim/types.hpp"

namespace client {

/// Draws the inventory/equipment panel for the local player from the world view
/// (equipment slots on top, backpack below), as a fixed-size overlay. Call
/// between begin_frame and end_frame when the panel is toggled on.
void draw_inventory(Renderer2D& renderer, const Tileset& tileset,
                    const WorldView& view);

/// Bottom edge of the inventory panel in window pixels (for stacking other HUD).
float inventory_panel_bottom(const Renderer2D& renderer);

/// Outer width of the inventory panel (shared with battle list alignment).
float inventory_panel_width();

/// What a click on the panel means. Equip = a backpack item clicked; Unequip = a
/// filled equipment slot clicked; None = the click missed the panel (so it should
/// fall through to the world).
struct InventoryAction {
    enum class Kind { None, Equip, Unequip };
    Kind            kind = Kind::None;
    sim::ItemTypeId item = sim::kItemNone;         ///< for Equip
    sim::EquipSlot  slot = sim::EquipSlot::Weapon;  ///< for Unequip
};

/// Hit-tests a window-pixel point against the panel using the same geometry as
/// draw_inventory.
InventoryAction inventory_hit(const Renderer2D& renderer, const WorldView& view,
                              float mouse_x, float mouse_y);

}  // namespace client

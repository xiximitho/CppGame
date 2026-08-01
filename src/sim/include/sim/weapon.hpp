#pragma once

#include <cstdint>

#include "sim/item_type.hpp"
#include "sim/tile_ids.hpp"
#include "sim/vocation_type.hpp"

// Weapon families and how well each vocation fights with them.
//
// A knight can equip a bow — the item system does not forbid it — but the damage
// multiplier makes that a bad idea. Same idea for a mage swinging a sword.
// Families are keyed by known item ids for now (no content.db schema bump).

namespace sim {

enum class WeaponFamily : std::uint8_t {
    None = 0,
    Sword,
    Bow,
    Staff,
};

/// Which family a weapon id belongs to. Non-weapons and unknown ids → None.
constexpr WeaponFamily weapon_family(ItemTypeId id) {
    if (id == tiles::kSword) {
        return WeaponFamily::Sword;
    }
    if (id == tiles::kBow) {
        return WeaponFamily::Bow;
    }
    if (id == tiles::kStaff) {
        return WeaponFamily::Staff;
    }
    return WeaponFamily::None;
}

/// Percent of weapon attack kept (100 = full). Armour defense is never scaled.
constexpr int vocation_weapon_percent(VocationId vocation, WeaponFamily family) {
    if (family == WeaponFamily::None) {
        return 100;
    }
    switch (vocation) {
        case vocations::kKnight:
            if (family == WeaponFamily::Sword) {
                return 100;
            }
            if (family == WeaponFamily::Bow) {
                return 40;
            }
            return 25;  // staff
        case vocations::kPaladin:
            if (family == WeaponFamily::Bow) {
                return 100;
            }
            if (family == WeaponFamily::Sword) {
                return 55;
            }
            return 25;
        case vocations::kMage:
        case vocations::kDruid:
            if (family == WeaponFamily::Staff) {
                return 100;
            }
            return 30;  // sword / bow
        case vocations::kRogue:
            if (family == WeaponFamily::Sword) {
                return 90;
            }
            if (family == WeaponFamily::Bow) {
                return 70;
            }
            return 25;
        case vocations::kNecro:
            if (family == WeaponFamily::Staff) {
                return 100;
            }
            return 30;
        default:
            return 100;  // no vocation yet: unchanged (legacy actors)
    }
}

}  // namespace sim

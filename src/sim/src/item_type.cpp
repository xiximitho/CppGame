#include "sim/item_type.hpp"

#include <cassert>

#include "sim/tile_ids.hpp"

namespace sim {

void ItemTypeRegistry::add(const ItemType& type) {
    assert(type.id != kItemNone);

    const auto index = static_cast<std::size_t>(type.id);
    if (index >= by_id_.size()) {
        by_id_.resize(index + 1U);  // gaps default to a kItemNone entry
    }

    // Overwriting an existing id is allowed (content patch); only a brand-new id
    // grows the registered count.
    if (by_id_[index].id == kItemNone) {
        ++count_;
    }
    by_id_[index] = type;
}

const ItemType& ItemTypeRegistry::get(ItemTypeId id) const {
    const auto index = static_cast<std::size_t>(id);
    if (id != kItemNone && index < by_id_.size() && by_id_[index].id == id) {
        return by_id_[index];
    }
    return none_;
}

std::vector<ItemTypeId> ItemTypeRegistry::ids() const {
    std::vector<ItemTypeId> out;
    out.reserve(count_);
    for (std::size_t i = 0; i < by_id_.size(); ++i) {
        if (by_id_[i].id != kItemNone) {
            out.push_back(static_cast<ItemTypeId>(i));
        }
    }
    return out;
}

bool ItemTypeRegistry::contains(ItemTypeId id) const {
    const auto index = static_cast<std::size_t>(id);
    return id != kItemNone && index < by_id_.size() &&
           by_id_[index].id == id;
}

ItemTypeRegistry build_default_registry() {
    ItemTypeRegistry registry;

    // Ground you can stand on. Water is ground too, but not walkable — today the
    // generator encodes that as a blocking empty object on a water tile; here it
    // is simply the water ground type carrying BlocksWalk, which is the cleaner
    // model the wiring step will adopt.
    registry.add(ItemType{tiles::kGrass, ItemFlag::Ground, 0U, 1U});
    registry.add(ItemType{tiles::kDirt,  ItemFlag::Ground, 0U, 1U});
    registry.add(ItemType{tiles::kStone, ItemFlag::Ground, 0U, 1U});
    registry.add(ItemType{tiles::kWater,
                          ItemFlag::Ground | ItemFlag::BlocksWalk, 0U, 1U});

    // Objects. A wall is solid to both movement and (eventually) sight; a tree
    // blocks the step but is scenery otherwise.
    registry.add(ItemType{tiles::kWall,
                          ItemFlag::BlocksWalk | ItemFlag::BlocksSight, 0U, 1U});
    registry.add(ItemType{tiles::kTree, ItemFlag::BlocksWalk, 0U, 1U});

    // Worked example (docs/sprites.md): a crate is a pushable-looking, pickable
    // blocker. Weight is set to show the field flowing through; nothing reads it
    // yet.
    registry.add(ItemType{tiles::kCrate,
                          ItemFlag::BlocksWalk | ItemFlag::Pickable, 40U, 1U});

    // Equipment. Weapons set attack + kind/range/effect; armour sets defense.
    registry.add(ItemType{.id = tiles::kSword, .flags = ItemFlag::Pickable,
                          .weight = 180U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 12,
                          .attack_kind = AttackKind::Melee, .attack_range = 1,
                          .effect = kEffectMeleeGlow});
    registry.add(ItemType{.id = tiles::kBow, .flags = ItemFlag::Pickable,
                          .weight = 90U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 8,
                          .attack_kind = AttackKind::Ranged, .attack_range = 4,
                          .effect = kEffectRangedShot});
    registry.add(ItemType{.id = tiles::kShield, .flags = ItemFlag::Pickable,
                          .weight = 90U, .equippable = true,
                          .slot = EquipSlot::Shield, .defense = 6});
    registry.add(ItemType{.id = tiles::kHelmet, .flags = ItemFlag::Pickable,
                          .weight = 40U, .equippable = true,
                          .slot = EquipSlot::Helmet, .defense = 3});
    registry.add(ItemType{.id = tiles::kArmor, .flags = ItemFlag::Pickable,
                          .weight = 120U, .equippable = true,
                          .slot = EquipSlot::Body, .defense = 8});
    registry.add(ItemType{.id = tiles::kLegs, .flags = ItemFlag::Pickable,
                          .weight = 70U, .equippable = true,
                          .slot = EquipSlot::Legs, .defense = 4});
    registry.add(ItemType{.id = tiles::kBoots, .flags = ItemFlag::Pickable,
                          .weight = 30U, .equippable = true,
                          .slot = EquipSlot::Boots, .defense = 2});
    registry.add(ItemType{.id = tiles::kRing, .flags = ItemFlag::Pickable,
                          .weight = 5U, .equippable = true,
                          .slot = EquipSlot::Ring, .defense = 1});
    registry.add(ItemType{.id = tiles::kAmulet, .flags = ItemFlag::Pickable,
                          .weight = 5U, .equippable = true,
                          .slot = EquipSlot::Amulet, .defense = 1});

    return registry;
}

}  // namespace sim

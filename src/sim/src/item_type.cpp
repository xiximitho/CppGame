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

    // Stairs. Not blockers: the whole point is to be stepped onto. The pair is
    // symmetric so the tile you land on can send you back.
    registry.add(ItemType{tiles::kStairsUp, ItemFlag::StairsUp, 0U, 1U});
    registry.add(ItemType{tiles::kStairsDown, ItemFlag::StairsDown, 0U, 1U});

    // The warp mouth: art and authoring anchor, no rule of its own. Not a blocker,
    // same as a stair. Where it leads is the map's `portal` line, per tile.
    registry.add(ItemType{tiles::kPortal, ItemFlag::Teleport, 0U, 1U});

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
    registry.add(ItemType{.id = tiles::kStaff, .flags = ItemFlag::Pickable,
                          .weight = 70U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 10,
                          .attack_kind = AttackKind::Ranged, .attack_range = 3,
                          .effect = kEffectFirebolt});
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

    // Class kits (OTSP icons). Tuned as starter gear — stronger than the
    // original 300–308 placeholders, still below endgame numbers.
    registry.add(ItemType{.id = tiles::kKnightSword, .flags = ItemFlag::Pickable,
                          .weight = 200U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 14,
                          .attack_kind = AttackKind::Melee, .attack_range = 1,
                          .effect = kEffectMeleeGlow});
    registry.add(ItemType{.id = tiles::kSteelShield, .flags = ItemFlag::Pickable,
                          .weight = 110U, .equippable = true,
                          .slot = EquipSlot::Shield, .defense = 8});
    registry.add(ItemType{.id = tiles::kSteelHelmet, .flags = ItemFlag::Pickable,
                          .weight = 55U, .equippable = true,
                          .slot = EquipSlot::Helmet, .defense = 5});
    registry.add(ItemType{.id = tiles::kPlateArmor, .flags = ItemFlag::Pickable,
                          .weight = 160U, .equippable = true,
                          .slot = EquipSlot::Body, .defense = 10});
    registry.add(ItemType{.id = tiles::kPlateLegs, .flags = ItemFlag::Pickable,
                          .weight = 90U, .equippable = true,
                          .slot = EquipSlot::Legs, .defense = 5});
    registry.add(ItemType{.id = tiles::kSteelBoots, .flags = ItemFlag::Pickable,
                          .weight = 40U, .equippable = true,
                          .slot = EquipSlot::Boots, .defense = 3});
    registry.add(ItemType{.id = tiles::kWoodenBow, .flags = ItemFlag::Pickable,
                          .weight = 80U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 9,
                          .attack_kind = AttackKind::Ranged, .attack_range = 4,
                          .effect = kEffectRangedShot});
    registry.add(ItemType{.id = tiles::kRangerTunic, .flags = ItemFlag::Pickable,
                          .weight = 70U, .equippable = true,
                          .slot = EquipSlot::Body, .defense = 6});
    registry.add(ItemType{.id = tiles::kLeatherLegs, .flags = ItemFlag::Pickable,
                          .weight = 45U, .equippable = true,
                          .slot = EquipSlot::Legs, .defense = 3});
    registry.add(ItemType{.id = tiles::kLeatherBoots, .flags = ItemFlag::Pickable,
                          .weight = 25U, .equippable = true,
                          .slot = EquipSlot::Boots, .defense = 2});
    registry.add(ItemType{.id = tiles::kCrossAmulet, .flags = ItemFlag::Pickable,
                          .weight = 5U, .equippable = true,
                          .slot = EquipSlot::Amulet, .defense = 2});
    registry.add(ItemType{.id = tiles::kQuiver, .flags = ItemFlag::Pickable,
                          .weight = 30U, .max_stack = 1U});
    registry.add(ItemType{.id = tiles::kBlueWand, .flags = ItemFlag::Pickable,
                          .weight = 50U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 12,
                          .attack_kind = AttackKind::Ranged, .attack_range = 4,
                          .effect = kEffectFirebolt});
    registry.add(ItemType{.id = tiles::kMageRobe, .flags = ItemFlag::Pickable,
                          .weight = 40U, .equippable = true,
                          .slot = EquipSlot::Body, .defense = 4});
    registry.add(ItemType{.id = tiles::kWizardHat, .flags = ItemFlag::Pickable,
                          .weight = 15U, .equippable = true,
                          .slot = EquipSlot::Helmet, .defense = 2});
    registry.add(ItemType{.id = tiles::kBlueBoots, .flags = ItemFlag::Pickable,
                          .weight = 20U, .equippable = true,
                          .slot = EquipSlot::Boots, .defense = 1});
    registry.add(ItemType{.id = tiles::kSapphireRing, .flags = ItemFlag::Pickable,
                          .weight = 5U, .equippable = true,
                          .slot = EquipSlot::Ring, .defense = 1});
    registry.add(ItemType{.id = tiles::kNatureStaff, .flags = ItemFlag::Pickable,
                          .weight = 55U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 11,
                          .attack_kind = AttackKind::Ranged, .attack_range = 4,
                          .effect = kEffectNature});
    registry.add(ItemType{.id = tiles::kLeatherArmor, .flags = ItemFlag::Pickable,
                          .weight = 65U, .equippable = true,
                          .slot = EquipSlot::Body, .defense = 5});
    registry.add(ItemType{.id = tiles::kWolfHood, .flags = ItemFlag::Pickable,
                          .weight = 18U, .equippable = true,
                          .slot = EquipSlot::Helmet, .defense = 2});
    registry.add(ItemType{.id = tiles::kGreenBoots, .flags = ItemFlag::Pickable,
                          .weight = 22U, .equippable = true,
                          .slot = EquipSlot::Boots, .defense = 2});
    registry.add(ItemType{.id = tiles::kMoonAmulet, .flags = ItemFlag::Pickable,
                          .weight = 5U, .equippable = true,
                          .slot = EquipSlot::Amulet, .defense = 2});
    registry.add(ItemType{.id = tiles::kHealthPotion,
                          .flags = ItemFlag::Pickable | ItemFlag::Stackable,
                          .weight = 20U, .max_stack = 20U});
    registry.add(ItemType{.id = tiles::kManaPotion,
                          .flags = ItemFlag::Pickable | ItemFlag::Stackable,
                          .weight = 20U, .max_stack = 20U});
    registry.add(ItemType{.id = tiles::kLongsword, .flags = ItemFlag::Pickable,
                          .weight = 190U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 13,
                          .attack_kind = AttackKind::Melee, .attack_range = 1,
                          .effect = kEffectMeleeGlow});
    registry.add(ItemType{.id = tiles::kRoundShield, .flags = ItemFlag::Pickable,
                          .weight = 80U, .equippable = true,
                          .slot = EquipSlot::Shield, .defense = 5});
    registry.add(ItemType{.id = tiles::kSturdyBow, .flags = ItemFlag::Pickable,
                          .weight = 95U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 10,
                          .attack_kind = AttackKind::Ranged, .attack_range = 5,
                          .effect = kEffectRangedShot});
    registry.add(ItemType{.id = tiles::kWoodenStaff, .flags = ItemFlag::Pickable,
                          .weight = 60U, .equippable = true,
                          .slot = EquipSlot::Weapon, .attack = 8,
                          .attack_kind = AttackKind::Ranged, .attack_range = 3,
                          .effect = kEffectNature});

    return registry;
}

}  // namespace sim

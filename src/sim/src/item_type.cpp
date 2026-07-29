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

    return registry;
}

}  // namespace sim

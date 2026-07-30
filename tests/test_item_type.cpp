#include <doctest/doctest.h>

#include "sim/item_type.hpp"
#include "sim/tile_ids.hpp"

using namespace sim;

TEST_CASE("ItemFlags combine and query") {
    const ItemFlags f = ItemFlag::Ground | ItemFlag::BlocksWalk;
    CHECK(f.has(ItemFlag::Ground));
    CHECK(f.has(ItemFlag::BlocksWalk));
    CHECK_FALSE(f.has(ItemFlag::BlocksSight));

    // A single flag is a valid ItemFlags value.
    const ItemFlags one = ItemFlag::Pickable;
    CHECK(one.has(ItemFlag::Pickable));
    CHECK_FALSE(one.has(ItemFlag::Ground));

    // Chaining onto an existing set.
    const ItemFlags three =
        (ItemFlag::Ground | ItemFlag::BlocksWalk) | ItemFlag::BlocksSight;
    CHECK(three.has(ItemFlag::BlocksSight));
    CHECK(three.has(ItemFlag::Ground));

    const ItemFlags none;
    CHECK(none.bits() == 0U);
    CHECK(none != f);
}

TEST_CASE("empty registry returns the none type for any id") {
    const ItemTypeRegistry registry;
    CHECK(registry.count() == 0U);
    CHECK_FALSE(registry.contains(tiles::kGrass));

    const ItemType& missing = registry.get(tiles::kGrass);
    CHECK(missing.id == kItemNone);
    CHECK(missing.flags == ItemFlags{});
    CHECK_FALSE(missing.blocks_walk());
}

TEST_CASE("registry stores and retrieves a type") {
    ItemTypeRegistry registry;
    registry.add(ItemType{tiles::kWall,
                          ItemFlag::BlocksWalk | ItemFlag::BlocksSight, 0U, 1U});

    CHECK(registry.count() == 1U);
    CHECK(registry.contains(tiles::kWall));

    const ItemType& wall = registry.get(tiles::kWall);
    CHECK(wall.id == tiles::kWall);
    CHECK(wall.blocks_walk());
    CHECK(wall.blocks_sight());

    // kItemNone is never a valid lookup, even on a populated registry.
    CHECK_FALSE(registry.contains(kItemNone));
    CHECK(registry.get(kItemNone).id == kItemNone);
}

TEST_CASE("re-adding an id overwrites without inflating the count") {
    ItemTypeRegistry registry;
    registry.add(ItemType{tiles::kTree, ItemFlag::BlocksWalk, 0U, 1U});
    registry.add(ItemType{tiles::kTree, ItemFlag::Pickable, 5U, 20U});

    CHECK(registry.count() == 1U);
    const ItemType& tree = registry.get(tiles::kTree);
    CHECK_FALSE(tree.blocks_walk());
    CHECK(tree.flags.has(ItemFlag::Pickable));
    CHECK(tree.weight == 5U);
    CHECK(tree.max_stack == 20U);
}

TEST_CASE("default registry has the base tile types plus the crate example") {
    const ItemTypeRegistry registry = build_default_registry();
    // 6 tiles + crate + 9 equipment items.
    CHECK(registry.count() == 16U);

    // Walkable ground.
    for (const ItemTypeId id :
         {tiles::kGrass, tiles::kDirt, tiles::kStone}) {
        CAPTURE(id);
        CHECK(registry.contains(id));
        CHECK(registry.get(id).is_ground());
        CHECK_FALSE(registry.get(id).blocks_walk());
    }

    // Water is ground you cannot stand on.
    CHECK(registry.get(tiles::kWater).is_ground());
    CHECK(registry.get(tiles::kWater).blocks_walk());

    // Objects that block the step.
    CHECK(registry.get(tiles::kWall).blocks_walk());
    CHECK(registry.get(tiles::kWall).blocks_sight());
    CHECK(registry.get(tiles::kTree).blocks_walk());
    CHECK_FALSE(registry.get(tiles::kTree).blocks_sight());

    // The crate example: a blocking, pickable object carrying a weight.
    CHECK(registry.get(tiles::kCrate).blocks_walk());
    CHECK(registry.get(tiles::kCrate).flags.has(ItemFlag::Pickable));
    CHECK(registry.get(tiles::kCrate).weight == 40U);

    // The actor id is not an item type and must not have leaked in.
    CHECK_FALSE(registry.contains(tiles::kActor));
}

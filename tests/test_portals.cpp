#include <doctest/doctest.h>

#include <cstdint>
#include <utility>

#include "sim/components.hpp"
#include "sim/item_type.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/world.hpp"

// The warp rule. It shares a call site with the stairs (see test_stairs.cpp) and
// most of these cases mirror one there, on purpose: the two mechanisms differ only
// in how the destination is worked out — relative from the item type for a stair,
// absolute per tile for a portal — and every refusal must behave the same.
//
// The portal cases the stairs cannot have: a destination anywhere on the map, and a
// return portal at that destination, which makes an infinite loop the DEFAULT
// authoring shape rather than a pathological one.

using namespace sim;

namespace {

/// Open stone, `floors` deep, big enough to hold two distant regions.
TileMap open_floors(int floors) {
    TileMap map(16, 16, floors);
    for (int z = 0; z < floors; ++z) {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                       static_cast<std::int16_t>(y),
                                       static_cast<std::int8_t>(z)},
                               tiles::kStone);
            }
        }
    }
    return map;
}

void run_until_still(World& world, NetId id) {
    const entt::entity entity = world.lookup(id);
    for (int i = 0; i < 64; ++i) {
        world.step();
        if (!world.registry().all_of<CWalk>(entity)) {
            return;
        }
    }
    FAIL("actor never finished its step");
}

TilePos tile_of(const World& world, NetId id) {
    return world.registry().get<CPosition>(world.lookup(id)).tile;
}

}  // namespace

TEST_CASE("stepping onto a portal lands the actor at its destination") {
    const ItemTypeRegistry items = build_default_registry();
    World world(open_floors(1), items);
    const TilePos gate{4, 4, 0};
    const TilePos landing{12, 9, 0};
    world.add_portal(gate, landing);

    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    CHECK(tile_of(world, id) == landing);
    // The tile stepped onto is released and the destination is held: no occupancy
    // left dangling on either end, which is what a half-applied move would leave.
    CHECK(world.occupant(gate) == kInvalidNetId);
    CHECK(world.occupant(landing) == id);
}

TEST_CASE("a portal reaches another floor without a stair") {
    const ItemTypeRegistry items = build_default_registry();
    World world(open_floors(3), items);
    world.add_portal(TilePos{4, 4, 0}, TilePos{10, 2, 2});

    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    CHECK(tile_of(world, id) == TilePos{10, 2, 2});
}

TEST_CASE("a portal pair does not ping-pong the actor") {
    // A return portal at the destination is how a two-way warp is authored, so
    // this is the normal case and not the exotic one: if the rule fired on arrival
    // by warp, every ordinary portal pair would teleport its user forever.
    const ItemTypeRegistry items = build_default_registry();
    World world(open_floors(1), items);
    const TilePos here{4, 4, 0};
    const TilePos there{12, 9, 0};
    world.add_portal(here, there);
    world.add_portal(there, here);

    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);
    REQUIRE(tile_of(world, id) == there);

    for (int i = 0; i < 60; ++i) {  // standing still on the return portal
        world.step();
    }
    CHECK(tile_of(world, id) == there);

    // Stepping off and back on is what sends it back, exactly like a stair.
    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);
    REQUIRE(tile_of(world, id) == TilePos{13, 9, 0});
    REQUIRE(world.request_walk(id, Direction::West));
    run_until_still(world, id);
    CHECK(tile_of(world, id) == here);
}

TEST_CASE("portals do not chain in one arrival") {
    // Landing on a tile that is itself a portal source does nothing until the
    // actor walks onto it. Same rule as above, stated for the case that looks like
    // it should chain — if chaining is ever wanted it is a new decision, not a bug.
    const ItemTypeRegistry items = build_default_registry();
    World world(open_floors(1), items);
    world.add_portal(TilePos{4, 4, 0}, TilePos{8, 8, 0});
    world.add_portal(TilePos{8, 8, 0}, TilePos{14, 14, 0});

    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    CHECK(tile_of(world, id) == TilePos{8, 8, 0});
}

TEST_CASE("a portal onto rock leaves the actor on the portal tile") {
    // The parser refuses to load such a map, but the World is also driven by the
    // editor and by tests, and geometry can be changed under a portal. Refusing
    // beats teleporting into a wall.
    const ItemTypeRegistry items = build_default_registry();
    TileMap map = open_floors(1);
    const TilePos rock{12, 9, 0};
    map.set_object(rock, tiles::kWall, items.get(tiles::kWall).blocks_walk());

    World world(std::move(map), items);
    const TilePos gate{4, 4, 0};
    world.add_portal(gate, rock);

    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    CHECK(tile_of(world, id) == gate);
    CHECK(world.occupant(gate) == id);
}

TEST_CASE("a portal onto an occupied tile leaves the actor on the portal tile") {
    const ItemTypeRegistry items = build_default_registry();
    World world(open_floors(1), items);
    const TilePos gate{4, 4, 0};
    const TilePos landing{12, 9, 0};
    world.add_portal(gate, landing);

    const NetId blocker = world.allocate_net_id();
    world.spawn_actor(blocker, landing, 0);
    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    CHECK(tile_of(world, id) == gate);
    CHECK(world.occupant(landing) == blocker);
}

TEST_CASE("taking a portal drops the route being followed") {
    // A route is a list of tiles from where the actor was. After a warp every one
    // of them is somewhere else entirely — a worse version of the stair case,
    // where at least the x,y stayed put.
    const ItemTypeRegistry items = build_default_registry();
    World world(open_floors(1), items);
    world.add_portal(TilePos{4, 4, 0}, TilePos{12, 9, 0});

    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{1, 4, 0}, 0);

    REQUIRE(world.request_move_to(id, TilePos{7, 4, 0}));  // crosses the portal
    for (int i = 0; i < 200; ++i) {
        world.step();
        update_path_followers(world);
        if (tile_of(world, id) == TilePos{12, 9, 0}) {
            break;
        }
    }

    CHECK(tile_of(world, id) == TilePos{12, 9, 0});
    CHECK_FALSE(world.is_following_path(id));
}

TEST_CASE("an explicit portal destination beats a stair on the same tile") {
    // Should not be authored, but the tie-break has to be predictable: the number
    // the author wrote wins over one derived from the item type.
    const ItemTypeRegistry items = build_default_registry();
    TileMap map = open_floors(2);
    const TilePos gate{4, 4, 0};
    map.set_object(gate, tiles::kStairsUp,
                   items.get(tiles::kStairsUp).blocks_walk());

    World world(std::move(map), items);
    world.add_portal(gate, TilePos{12, 9, 0});

    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    CHECK(tile_of(world, id) == TilePos{12, 9, 0});
}

TEST_CASE("a portal onto its own tile is dropped, not stored") {
    World world(open_floors(1), build_default_registry());
    world.add_portal(TilePos{4, 4, 0}, TilePos{4, 4, 0});
    CHECK(world.portal_count() == 0);
    CHECK_FALSE(world.portal_at(TilePos{4, 4, 0}).has_value());

    world.add_portal(TilePos{4, 4, 0}, TilePos{5, 5, 0});
    CHECK(world.portal_count() == 1);
    REQUIRE(world.portal_at(TilePos{4, 4, 0}).has_value());
    CHECK(*world.portal_at(TilePos{4, 4, 0}) == TilePos{5, 5, 0});
}

TEST_CASE("a monster steps through a portal like anyone else") {
    // Not a special case in the code, and this test exists to keep it that way:
    // the transition reads CPosition and CActor, which every actor has. A rule
    // that only moved players would behave differently in solo play, where the
    // local actor has no CPlayer at all.
    const ItemTypeRegistry items = build_default_registry();
    World world(open_floors(1), items);
    const TilePos gate{4, 4, 0};
    const TilePos landing{12, 9, 0};
    world.add_portal(gate, landing);

    const entt::entity mob =
        spawn_monster(world, monsters::kRat, TilePos{3, 4, 0});
    REQUIRE((mob != entt::null));
    const NetId id = world.registry().get<CActor>(mob).net_id;

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    CHECK(tile_of(world, id) == landing);
}

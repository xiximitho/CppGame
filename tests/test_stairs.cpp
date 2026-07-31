#include <doctest/doctest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "sim/components.hpp"
#include "sim/content_blob.hpp"
#include "sim/item_type.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/world.hpp"

using namespace sim;

namespace {

/// Two floors of open stone, `floors` deep, with nothing on them. Stairs are
/// placed by each test so the geometry under test is visible in the test itself.
TileMap open_floors(int floors) {
    TileMap map(8, 8, floors);
    for (int z = 0; z < floors; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                       static_cast<std::int16_t>(y),
                                       static_cast<std::int8_t>(z)},
                               tiles::kStone);
            }
        }
    }
    return map;
}

void put_object(TileMap& map, TilePos at, TileId id,
                const ItemTypeRegistry& items) {
    map.set_object(at, id, items.get(id).blocks_walk());
}

/// Runs the world until the step in flight has landed. A step is a handful of
/// ticks; the cap keeps a broken rule from hanging the suite.
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

}  // namespace

TEST_CASE("walking onto a stair moves the actor a floor") {
    const ItemTypeRegistry items = build_default_registry();
    TileMap map = open_floors(2);
    const TilePos stair{4, 4, 0};
    put_object(map, stair, tiles::kStairsUp, items);

    World world(std::move(map), items);
    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    const auto& pos = world.registry().get<CPosition>(world.lookup(id));
    CHECK(pos.tile.z == 1);
    CHECK(pos.tile.x == 4);
    CHECK(pos.tile.y == 4);

    // The tile left behind on floor 0 is free, and the one landed on is held.
    CHECK(world.occupant(stair) == kInvalidNetId);
    CHECK(world.occupant(TilePos{4, 4, 1}) == id);
}

TEST_CASE("a stair pair does not ping-pong the actor") {
    // The classic bug: the tile above holds the matching down-stair, so a rule
    // that fires on arrival-by-teleport sends the actor back down forever.
    const ItemTypeRegistry items = build_default_registry();
    TileMap map = open_floors(2);
    put_object(map, TilePos{4, 4, 0}, tiles::kStairsUp, items);
    put_object(map, TilePos{4, 4, 1}, tiles::kStairsDown, items);

    World world(std::move(map), items);
    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);
    CHECK(world.registry().get<CPosition>(world.lookup(id)).tile.z == 1);

    for (int i = 0; i < 30; ++i) {  // standing still on the down-stair
        world.step();
    }
    CHECK(world.registry().get<CPosition>(world.lookup(id)).tile.z == 1);

    // Stepping off and back on is what takes it down again.
    REQUIRE(world.request_walk(id, Direction::West));
    run_until_still(world, id);
    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);
    CHECK(world.registry().get<CPosition>(world.lookup(id)).tile.z == 0);
}

TEST_CASE("a stair with no floor above leaves the actor where it is") {
    const ItemTypeRegistry items = build_default_registry();
    TileMap map = open_floors(1);  // nothing above floor 0
    put_object(map, TilePos{4, 4, 0}, tiles::kStairsUp, items);

    World world(std::move(map), items);
    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    const auto& pos = world.registry().get<CPosition>(world.lookup(id));
    CHECK(pos.tile.z == 0);
    CHECK(pos.tile.x == 4);
    CHECK(world.occupant(TilePos{4, 4, 0}) == id);
}

TEST_CASE("a stair into a wall leaves the actor where it is") {
    const ItemTypeRegistry items = build_default_registry();
    TileMap map = open_floors(2);
    put_object(map, TilePos{4, 4, 0}, tiles::kStairsUp, items);
    put_object(map, TilePos{4, 4, 1}, tiles::kWall, items);  // ceiling is rock

    World world(std::move(map), items);
    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);
    CHECK(world.registry().get<CPosition>(world.lookup(id)).tile.z == 0);
}

TEST_CASE("a stair onto an occupied tile leaves the actor where it is") {
    const ItemTypeRegistry items = build_default_registry();
    TileMap map = open_floors(2);
    put_object(map, TilePos{4, 4, 0}, tiles::kStairsUp, items);

    World world(std::move(map), items);
    const NetId blocker = world.allocate_net_id();
    world.spawn_actor(blocker, TilePos{4, 4, 1}, 0);
    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{3, 4, 0}, 0);

    REQUIRE(world.request_walk(id, Direction::East));
    run_until_still(world, id);

    CHECK(world.registry().get<CPosition>(world.lookup(id)).tile.z == 0);
    CHECK(world.occupant(TilePos{4, 4, 1}) == blocker);
}

TEST_CASE("taking a stair drops the route being followed") {
    // A path is a list of tiles on one floor. Keeping it after a floor change
    // would walk the actor through geometry the pathfinder never looked at.
    const ItemTypeRegistry items = build_default_registry();
    TileMap map = open_floors(2);
    put_object(map, TilePos{4, 4, 0}, tiles::kStairsUp, items);

    World world(std::move(map), items);
    const NetId id = world.allocate_net_id();
    world.spawn_actor(id, TilePos{1, 4, 0}, 0);

    REQUIRE(world.request_move_to(id, TilePos{7, 4, 0}));  // route crosses the stair
    for (int i = 0; i < 200; ++i) {
        world.step();
        update_path_followers(world);
        if (world.registry().get<CPosition>(world.lookup(id)).tile.z == 1) {
            break;
        }
    }

    CHECK(world.registry().get<CPosition>(world.lookup(id)).tile.z == 1);
    CHECK_FALSE(world.is_following_path(id));
}

TEST_CASE("stair flags survive the content blob round trip") {
    // The flags travel to the client inside the baked blob. A field width that
    // dropped the two new bits would show up as stairs that work on the server
    // and do nothing in solo play — the kind of split nobody looks for.
    const ItemTypeRegistry items = build_default_registry();
    CHECK(items.get(tiles::kStairsUp).stair_delta_z() == 1);
    CHECK(items.get(tiles::kStairsDown).stair_delta_z() == -1);
    CHECK(items.get(tiles::kStone).stair_delta_z() == 0);
    CHECK_FALSE(items.get(tiles::kStairsUp).blocks_walk());
    CHECK_FALSE(items.get(tiles::kStairsDown).blocks_walk());

    const std::vector<std::uint8_t> blob = write_content_blob(items);
    ItemTypeRegistry decoded;
    REQUIRE(read_content_blob(blob.data(), blob.size(), decoded));
    CHECK(decoded.get(tiles::kStairsUp) == items.get(tiles::kStairsUp));
    CHECK(decoded.get(tiles::kStairsDown) == items.get(tiles::kStairsDown));
    CHECK(decoded.get(tiles::kStairsUp).stair_delta_z() == 1);
    CHECK(decoded.get(tiles::kStairsDown).stair_delta_z() == -1);
}

#include <doctest/doctest.h>

#include "sim/components.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/world.hpp"

using namespace sim;

namespace {

/// A small all-walkable stone map.
World make_open_world() {
    TileMap map(12, 12, 1);
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                   static_cast<std::int16_t>(y), 0},
                           tiles::kStone);
        }
    }
    return World(std::move(map));
}

std::int32_t hp_of(World& world, NetId id) {
    return world.registry().get<CHealth>(world.lookup(id)).hp;
}

// One simulation tick with combat resolved after it, as the sessions do.
void tick(World& world) {
    world.step();
    update_combat(world);
}

}  // namespace

TEST_CASE("an adjacent target takes damage on cooldown and a monster despawns") {
    World world = make_open_world();
    const NetId attacker = world.allocate_net_id();
    world.spawn_actor(attacker, TilePos{5, 5, 0}, 0);
    const NetId target = world.allocate_net_id();
    world.spawn_actor(target, TilePos{6, 5, 0}, 0);

    world.set_attack_target(attacker, target);

    // First tick lands the first swing (cooldown starts at 0).
    tick(world);
    CHECK(hp_of(world, target) == 100 - kBaseMeleeDamage);

    // No second swing until the cooldown elapses.
    tick(world);
    CHECK(hp_of(world, target) == 100 - kBaseMeleeDamage);

    // The attacker turned to face the target (east).
    CHECK(world.registry().get<CPosition>(world.lookup(attacker)).facing ==
          Direction::East);

    // Run well past enough swings to kill it. No CRespawn -> despawned.
    for (int i = 0; i < 220; ++i) {
        tick(world);
    }
    CHECK((world.lookup(target) == entt::null));
    // The attacker's now-dangling target was dropped.
    CHECK_FALSE(world.registry().all_of<CTarget>(world.lookup(attacker)));
}

TEST_CASE("a target out of melee range is never hit") {
    World world = make_open_world();
    const NetId attacker = world.allocate_net_id();
    world.spawn_actor(attacker, TilePos{2, 2, 0}, 0);
    const NetId target = world.allocate_net_id();
    world.spawn_actor(target, TilePos{5, 2, 0}, 0);  // three tiles away

    world.set_attack_target(attacker, target);
    for (int i = 0; i < 120; ++i) {
        tick(world);
    }
    CHECK(hp_of(world, target) == 100);
}

TEST_CASE("a player (CRespawn) dies then respawns at its point") {
    World world = make_open_world();
    const NetId attacker = world.allocate_net_id();
    world.spawn_actor(attacker, TilePos{5, 5, 0}, 0);
    const NetId player = world.allocate_net_id();
    const entt::entity player_entity =
        world.spawn_actor(player, TilePos{6, 5, 0}, 0);
    world.registry().emplace<CRespawn>(player_entity, CRespawn{TilePos{1, 1, 0}});

    world.set_attack_target(attacker, player);

    // Kill it.
    int guard = 0;
    while (!world.registry().all_of<CDead>(world.lookup(player)) &&
           guard++ < 400) {
        tick(world);
    }
    REQUIRE(world.registry().all_of<CDead>(world.lookup(player)));
    CHECK(hp_of(world, player) == 0);
    // Dead: no longer occupies its tile, so it does not block movement.
    CHECK(world.occupant(TilePos{6, 5, 0}) == kInvalidNetId);

    // Wait out the respawn timer.
    for (int i = 0; i < static_cast<int>(kRespawnTicks) + 5; ++i) {
        tick(world);
    }
    CHECK_FALSE(world.registry().all_of<CDead>(world.lookup(player)));
    CHECK(hp_of(world, player) == 100);
    const CPosition& pos = world.registry().get<CPosition>(world.lookup(player));
    CHECK(pos.tile.x == 1);
    CHECK(pos.tile.y == 1);
    CHECK(world.occupant(TilePos{1, 1, 0}) == player);
}

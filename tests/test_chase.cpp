#include <doctest/doctest.h>

#include <utility>

#include "sim/components.hpp"
#include "sim/monster_type.hpp"
#include "sim/rng.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/world.hpp"

using namespace sim;

namespace {

World open_world(int w = 20, int h = 20) {
    TileMap map(w, h, 1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                   static_cast<std::int16_t>(y), 0},
                           tiles::kStone);
        }
    }
    return World(std::move(map), build_default_registry());
}

void wall(World& world, int x, int y) {
    const TilePos at{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), 0};
    world.map().set_object(at, tiles::kWall,
                           world.item_types().get(tiles::kWall).blocks_walk());
}

/// The real tick order, the one the server and the solo session both run.
void run(World& world, Rng& rng, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        world.step();
        update_monsters(world, rng);
        update_chasers(world);
        update_path_followers(world);
        update_combat(world);
    }
}

/// A non-monster actor, which is what aggro and chasing key on.
///
/// `hp` exists because a target that DIES changes what is being measured: a dead
/// actor with CRespawn comes back at full health, so "did it take damage" reads
/// false, and a dead one without CRespawn vanishes and the chase correctly ends.
/// A punching bag keeps the test about the chase.
NetId spawn_fighter(World& world, TilePos at, std::int16_t attack = 0,
                    std::int32_t hp = 100) {
    const NetId id = world.allocate_net_id();
    const entt::entity entity = world.spawn_actor(id, at, kAppearancePlayer);
    world.registry().emplace<CRespawn>(entity, CRespawn{at});
    world.registry().replace<CHealth>(entity, CHealth{hp, hp});
    if (attack != 0) {
        world.registry().emplace<CCombat>(entity, CCombat{attack, 0, 1,
                                                         kEffectMeleeGlow});
    }
    return id;
}

TilePos tile_of(const World& world, NetId id) {
    return world.registry().get<CPosition>(world.lookup(id)).tile;
}

}  // namespace

TEST_CASE("a chase follows a target that keeps moving") {
    // The bug this exists for: clicking a creature planned ONE route to the tile it
    // stood on. It walked away, the route ran out, and the attacker stood there
    // holding a target it never reached and never hit.
    World world = open_world(40, 12);
    Rng rng(4);

    const NetId hunter = spawn_fighter(world, TilePos{2, 6, 0});
    const NetId prey = spawn_fighter(world, TilePos{10, 6, 0});

    world.set_attack_target(hunter, prey);
    world.request_follow(hunter, prey);

    // The prey walks east the whole time, so every route the hunter plans is stale
    // a moment later. A chase has to keep replanning.
    for (int i = 0; i < 300; ++i) {
        world.step();
        update_chasers(world);
        update_path_followers(world);
        update_combat(world);
        if (i % 12 == 0) {
            world.request_walk(prey, Direction::East);
        }
        if (tile_distance(tile_of(world, hunter), tile_of(world, prey)) <= 1) {
            break;
        }
    }

    CHECK(tile_distance(tile_of(world, hunter), tile_of(world, prey)) <= 1);
    CHECK(world.registry().all_of<CFollow>(world.lookup(hunter)));
}

TEST_CASE("a chase walks around a wall instead of stalling against it") {
    // The other half of the same bug: a monster two tiles away stepped greedily
    // toward its prey, the diagonal was refused by the corner rule, and it retried
    // the same refused step forever — parked next to the player, not attacking.
    World world = open_world(20, 20);
    Rng rng(8);

    //  hunter at (5,10), prey at (9,10), a wall column between them with a gap
    //  far to the north, so the only way through is around.
    for (int y = 4; y <= 19; ++y) {
        wall(world, 7, y);
    }

    const NetId hunter = spawn_fighter(world, TilePos{5, 10, 0});
    const NetId prey = spawn_fighter(world, TilePos{9, 10, 0});
    world.set_attack_target(hunter, prey);
    world.request_follow(hunter, prey);

    run(world, rng, 400);

    // It got there: the only route was north around the wall.
    CHECK(tile_distance(tile_of(world, hunter), tile_of(world, prey)) <= 1);
}

TEST_CASE("a chase stops walking once the target is in reach and keeps hitting") {
    World world = open_world();
    Rng rng(2);

    const NetId hunter = spawn_fighter(world, TilePos{4, 4, 0}, 10);
    const NetId prey = spawn_fighter(world, TilePos{9, 4, 0}, 0, 100000);
    world.set_attack_target(hunter, prey);
    world.request_follow(hunter, prey);

    run(world, rng, 200);

    const entt::entity hunter_entity = world.lookup(hunter);
    // Standing still next to it, not shoving at an occupied tile.
    CHECK(tile_distance(tile_of(world, hunter), tile_of(world, prey)) <= 1);
    CHECK_FALSE(world.registry().all_of<CPathFollow>(hunter_entity));
    // And the swings landed: this is the "stopped attacking" half of the report.
    CHECK(world.registry().get<CHealth>(world.lookup(prey)).hp < 100000);
}

TEST_CASE("an ogre's reach means it stops two tiles out") {
    World world = open_world();
    Rng rng(6);

    const entt::entity ogre =
        spawn_monster(world, monsters::kOgre, TilePos{4, 4, 0});
    REQUIRE((ogre != entt::null));
    const NetId ogre_id = world.registry().get<CActor>(ogre).net_id;
    const NetId prey = spawn_fighter(world, TilePos{12, 4, 0}, 0, 100000);

    world.set_attack_target(ogre_id, prey);
    world.request_follow(ogre_id, prey);
    run(world, rng, 400);

    const int distance = tile_distance(tile_of(world, ogre_id), tile_of(world, prey));
    CHECK(distance <= 2);
    CHECK(distance >= 1);  // it did not need to be adjacent
    CHECK(world.registry().get<CHealth>(world.lookup(prey)).hp < 100000);
}

TEST_CASE("manual movement takes control back from a chase") {
    World world = open_world();
    Rng rng(1);

    const NetId hunter = spawn_fighter(world, TilePos{4, 4, 0});
    const NetId prey = spawn_fighter(world, TilePos{14, 4, 0});
    world.set_attack_target(hunter, prey);
    world.request_follow(hunter, prey);
    run(world, rng, 30);
    REQUIRE(world.registry().all_of<CFollow>(world.lookup(hunter)));

    // What the input handlers do for a key press or a click on the ground.
    world.cancel_path(hunter);

    CHECK_FALSE(world.registry().all_of<CFollow>(world.lookup(hunter)));
    CHECK_FALSE(world.registry().all_of<CPathFollow>(world.lookup(hunter)));

    // The step already in flight still lands — an actor is never yanked backwards
    // off a tile it is halfway onto — so settle first, then check it stays put.
    run(world, rng, 20);
    const TilePos before = tile_of(world, hunter);
    run(world, rng, 90);
    // It stays put: no chase resumes behind the player's back.
    CHECK(tile_of(world, hunter) == before);
}

TEST_CASE("a chase ends when the target dies") {
    World world = open_world();
    Rng rng(7);

    const NetId hunter = spawn_fighter(world, TilePos{4, 4, 0}, 50);
    const entt::entity rat = spawn_monster(world, monsters::kRat, TilePos{7, 4, 0});
    const NetId rat_id = world.registry().get<CActor>(rat).net_id;

    world.set_attack_target(hunter, rat_id);
    world.request_follow(hunter, rat_id);
    run(world, rng, 300);

    CHECK((world.lookup(rat_id) == entt::null));  // a monster does not respawn
    const entt::entity hunter_entity = world.lookup(hunter);
    CHECK_FALSE(world.registry().all_of<CFollow>(hunter_entity));
    CHECK_FALSE(world.registry().all_of<CTarget>(hunter_entity));
}

TEST_CASE("re-issuing the same target does not restart the chase every frame") {
    // A client that sends the attack intent once per frame must not keep resetting
    // the plan, or the actor replans forever and never takes a step.
    World world = open_world();
    const NetId hunter = spawn_fighter(world, TilePos{4, 4, 0});
    const NetId prey = spawn_fighter(world, TilePos{14, 4, 0});

    world.request_follow(hunter, prey);
    const entt::entity entity = world.lookup(hunter);
    world.registry().get<CFollow>(entity).next_replan_tick = 12345;

    world.request_follow(hunter, prey);  // same target again
    CHECK(world.registry().get<CFollow>(entity).next_replan_tick == 12345);

    world.request_follow(hunter, kInvalidNetId);  // clearing works
    CHECK_FALSE(world.registry().all_of<CFollow>(entity));
}

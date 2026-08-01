#include <doctest/doctest.h>

#include <utility>
#include <vector>

#include "sim/components.hpp"
#include "sim/monster_type.hpp"
#include "sim/rng.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/world.hpp"

using namespace sim;

namespace {

/// One open floor of stone, big enough for a chase to need several steps.
World open_world(int w = 24, int h = 24) {
    const ItemTypeRegistry items = build_default_registry();
    TileMap map(w, h, 1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                   static_cast<std::int16_t>(y), 0},
                           tiles::kStone);
        }
    }
    return World(std::move(map), items);
}

/// Runs the world the way both the server loop and the solo session do.
void run(World& world, Rng& rng, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        world.step();
        update_spawners(world, rng);
        update_monsters(world, rng);
        update_chasers(world);
        update_path_followers(world);
        update_combat(world);
    }
}

/// A stand-in for the local player: an actor with no CMonster, which is what
/// aggro keys on. It gets CRespawn like the real thing, so a monster killing it
/// leaves an entity to inspect instead of a dangling id.
NetId spawn_player(World& world, TilePos at) {
    const NetId id = world.allocate_net_id();
    const entt::entity entity = world.spawn_actor(id, at, kAppearancePlayer);
    world.registry().emplace<CRespawn>(entity, CRespawn{at});
    return id;
}

}  // namespace

TEST_CASE("a monster class's numbers land on the actor it spawns") {
    World world = open_world();
    const entt::entity ogre =
        spawn_monster(world, monsters::kOgre, TilePos{5, 5, 0});
    REQUIRE((ogre != entt::null));

    const MonsterType& spec = default_monsters().get(monsters::kOgre);
    const entt::registry& registry = world.registry();

    CHECK(registry.get<CActor>(ogre).appearance == spec.appearance);
    CHECK(registry.get<CActor>(ogre).step_ticks == spec.step_ticks);
    CHECK(registry.get<CHealth>(ogre).max_hp == spec.max_hp);
    CHECK(registry.get<CMonster>(ogre).type == monsters::kOgre);

    // Innate stats reach combat through CCombat, with no gear involved: that is
    // what makes one class hit harder than another.
    const CombatStats stats = combat_stats(world, ogre);
    CHECK(stats.attack == kBaseMeleeDamage + spec.attack);
    CHECK(stats.defense == spec.defense);
    CHECK(stats.range == spec.attack_range);
}

TEST_CASE("the three shipped classes actually differ") {
    const MonsterRegistry& all = default_monsters();
    const MonsterType& rat = all.get(monsters::kRat);
    const MonsterType& skeleton = all.get(monsters::kSkeleton);
    const MonsterType& ogre = all.get(monsters::kOgre);

    CHECK(all.count() == 3U);
    // Speed, health and damage all order the same way, so the classes are not
    // three copies with a different number of hit points.
    CHECK(rat.step_ticks < skeleton.step_ticks);
    CHECK(skeleton.step_ticks < ogre.step_ticks);
    CHECK(rat.max_hp < skeleton.max_hp);
    CHECK(skeleton.max_hp < ogre.max_hp);
    CHECK(rat.attack < skeleton.attack);
    CHECK(skeleton.attack < ogre.attack);
    CHECK(ogre.attack_range == 2);  // the one that reaches past adjacent tiles

    // Distinct sprites, or players cannot tell them apart.
    CHECK(rat.appearance != skeleton.appearance);
    CHECK(skeleton.appearance != ogre.appearance);
}

TEST_CASE("an unknown class spawns nothing") {
    World world = open_world();
    CHECK((spawn_monster(world, 9999, TilePos{2, 2, 0}) == entt::null));
    CHECK((spawn_monster(world, kMonsterNone, TilePos{2, 2, 0}) == entt::null));
}

TEST_CASE("a monster closes on a player inside its aggro radius") {
    World world = open_world();
    Rng rng(7);

    const entt::entity skeleton =
        spawn_monster(world, monsters::kSkeleton, TilePos{4, 10, 0});
    const NetId player = spawn_player(world, TilePos{12, 10, 0});
    REQUIRE((skeleton != entt::null));

    const int before =
        tile_distance(world.registry().get<CPosition>(skeleton).tile,
                      world.registry().get<CPosition>(world.lookup(player)).tile);
    run(world, rng, 90);  // three seconds; a skeleton steps every 19 ticks
    const int after =
        tile_distance(world.registry().get<CPosition>(skeleton).tile,
                      world.registry().get<CPosition>(world.lookup(player)).tile);

    CHECK(after < before);
    // And it is attacking, not just walking at it.
    CHECK(world.registry().all_of<CTarget>(skeleton));
    CHECK(world.registry().get<CTarget>(skeleton).target == player);
}

TEST_CASE("a monster ignores a player it cannot see and stays near home") {
    World world = open_world(40, 40);
    Rng rng(11);

    const TilePos home{5, 5, 0};
    const entt::entity rat = spawn_monster(world, monsters::kRat, home);
    spawn_player(world, TilePos{35, 35, 0});  // far outside a rat's 4-tile radius
    REQUIRE((rat != entt::null));

    run(world, rng, 300);  // ten seconds of idling

    const CMonster& spec = world.registry().get<CMonster>(rat);
    const TilePos at = world.registry().get<CPosition>(rat).tile;
    CHECK_FALSE(world.registry().all_of<CTarget>(rat));
    CHECK(tile_distance(at, home) <= static_cast<int>(spec.leash));
}

TEST_CASE("monsters do not hunt each other") {
    // Aggro is 'anything that is not a monster', which in solo play is the local
    // actor even though it has no CPlayer. Two monsters side by side must sit there.
    World world = open_world();
    Rng rng(3);

    const entt::entity a =
        spawn_monster(world, monsters::kSkeleton, TilePos{10, 10, 0});
    const entt::entity b =
        spawn_monster(world, monsters::kSkeleton, TilePos{11, 10, 0});
    REQUIRE((a != entt::null));
    REQUIRE((b != entt::null));

    run(world, rng, 120);

    CHECK_FALSE(world.registry().all_of<CTarget>(a));
    CHECK_FALSE(world.registry().all_of<CTarget>(b));
    CHECK(world.registry().get<CHealth>(a).hp ==
          default_monsters().get(monsters::kSkeleton).max_hp);
}

TEST_CASE("authored spawns skip what they cannot place") {
    World world = open_world();
    const ItemTypeRegistry& items = world.item_types();
    world.map().set_object(TilePos{3, 3, 0}, tiles::kWall,
                           items.get(tiles::kWall).blocks_walk());
    spawn_player(world, TilePos{6, 6, 0});  // occupies its tile

    const std::vector<MonsterSpawn> list{
        {TilePos{2, 2, 0}, monsters::kRat},       // fine
        {TilePos{3, 3, 0}, monsters::kRat},       // inside a wall
        {TilePos{6, 6, 0}, monsters::kSkeleton},  // tile already taken
        {TilePos{4, 4, 0}, 4242},                 // class does not exist
        {TilePos{5, 4, 0}, monsters::kOgre},      // fine
    };

    CHECK(spawn_authored_monsters(world, list) == 2);
    CHECK(world.occupant(TilePos{2, 2, 0}) != kInvalidNetId);
    CHECK(world.occupant(TilePos{5, 4, 0}) != kInvalidNetId);
    CHECK(world.occupant(TilePos{4, 4, 0}) == kInvalidNetId);
}

TEST_CASE("a monster leaves a phantom corpse with class loot when it dies") {
    World world = open_world();
    const entt::entity rat = spawn_monster(world, monsters::kRat, TilePos{8, 8, 0});
    REQUIRE((rat != entt::null));
    const NetId id = world.registry().get<CActor>(rat).net_id;
    const TilePos where{8, 8, 0};

    REQUIRE(world.apply_damage(id, default_monsters().get(monsters::kRat).max_hp));

    // No CRespawn on a monster: it is gone; loot sits on a CCorpse, not the floor.
    CHECK((world.lookup(id) == entt::null));
    CHECK(world.ground_items_at(where) == nullptr);
    CHECK(world.occupant(where) == kInvalidNetId);  // corpse is not occupancy

    entt::entity corpse = entt::null;
    for (const auto [entity, pos] :
         world.registry().view<CCorpse, CPosition>().each()) {
        if (pos.tile == where) {
            corpse = entity;
            break;
        }
    }
    REQUIRE((corpse != entt::null));
    const auto& pack = world.registry().get<CInventory>(corpse);
    REQUIRE(pack.items.size() == 1U);
    CHECK(pack.items[0].id ==
          default_monsters().get(monsters::kRat).loot_table[0].item);
}

TEST_CASE("walking onto a corpse does not auto-loot; take_from_corpse does") {
    World world = open_world();
    const TilePos where{6, 5, 0};
    const NetId hero = world.allocate_net_id();
    world.spawn_actor(hero, TilePos{5, 5, 0}, 0);
    world.registry().emplace<CInventory>(world.lookup(hero));

    const entt::entity rat = spawn_monster(world, monsters::kRat, where);
    REQUIRE((rat != entt::null));
    const NetId mob = world.registry().get<CActor>(rat).net_id;
    REQUIRE(world.apply_damage(mob, default_monsters().get(monsters::kRat).max_hp));
    REQUIRE_FALSE(world.registry().view<CCorpse>().empty());

    world.request_walk(hero, Direction::East);
    for (int i = 0; i < static_cast<int>(kDefaultStepTicks) + 3; ++i) {
        world.step();
    }
    // Still there: walk-over no longer scoops the bag.
    REQUIRE_FALSE(world.registry().view<CCorpse>().empty());
    CHECK(world.registry().get<CInventory>(world.lookup(hero)).items.empty());

    REQUIRE(world.take_from_corpse(hero, where, 0));
    CHECK(world.registry().view<CCorpse>().empty());
    const auto& pack = world.registry().get<CInventory>(world.lookup(hero));
    REQUIRE_FALSE(pack.items.empty());
    CHECK(pack.items[0].id ==
          default_monsters().get(monsters::kRat).loot_table[0].item);
}

TEST_CASE("take_from_corpse refuses when the taker is too far") {
    World world = open_world();
    const TilePos where{8, 8, 0};
    const NetId hero = world.allocate_net_id();
    world.spawn_actor(hero, TilePos{2, 2, 0}, 0);
    world.registry().emplace<CInventory>(world.lookup(hero));

    const entt::entity rat = spawn_monster(world, monsters::kRat, where);
    const NetId mob = world.registry().get<CActor>(rat).net_id;
    REQUIRE(world.apply_damage(mob, default_monsters().get(monsters::kRat).max_hp));

    CHECK_FALSE(world.take_from_corpse(hero, where, 0));
    CHECK_FALSE(world.registry().view<CCorpse>().empty());
}

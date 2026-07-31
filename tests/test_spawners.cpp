#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include "sim/components.hpp"
#include "sim/monster_io.hpp"
#include "sim/monster_type.hpp"
#include "sim/rng.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/world.hpp"

using namespace sim;

namespace {

World open_world(MonsterRegistry monsters = default_monsters()) {
    TileMap map(16, 16, 1);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                   static_cast<std::int16_t>(y), 0},
                           tiles::kStone);
        }
    }
    return World(std::move(map), build_default_registry(), std::move(monsters));
}

/// Ticks the spawner system the way both loops do.
void run(World& world, Rng& rng, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        world.step();
        update_spawners(world, rng);
    }
}

std::size_t live_monsters(const World& world) {
    return world.registry().view<CMonster>().size();
}

}  // namespace

TEST_CASE("a spawner fills up to its population and then stops") {
    World world = open_world();
    Rng rng(5);

    // Three rats, radius 2, one every two seconds.
    REQUIRE(create_spawners(world, {{TilePos{8, 8, 0}, monsters::kRat, 3, 2, 2}}) ==
            1);
    CHECK(live_monsters(world) == 0U);

    run(world, rng, 1);            // the first one is due immediately
    CHECK(live_monsters(world) == 1U);

    run(world, rng, 2 * kSimHz);   // trickles, one per period
    CHECK(live_monsters(world) == 2U);

    run(world, rng, 10 * kSimHz);  // and never exceeds the population
    CHECK(live_monsters(world) == 3U);
}

TEST_CASE("clearing a nest keeps it clear for the respawn time") {
    World world = open_world();
    Rng rng(9);

    REQUIRE(create_spawners(world, {{TilePos{8, 8, 0}, monsters::kRat, 1, 1, 30}}) ==
            1);
    run(world, rng, 1);
    REQUIRE(live_monsters(world) == 1U);

    // Kill it. A monster has no CRespawn, so it is gone from the world.
    const entt::entity rat = *world.registry().view<CMonster>().begin();
    world.apply_damage(world.registry().get<CActor>(rat).net_id, 9999);
    REQUIRE(live_monsters(world) == 0U);

    run(world, rng, 20 * kSimHz);   // ten seconds short of the timer
    CHECK(live_monsters(world) == 0U);

    run(world, rng, 11 * kSimHz);
    CHECK(live_monsters(world) == 1U);
}

TEST_CASE("a spawner never places a mob in rock or on top of someone") {
    World world = open_world();
    Rng rng(3);

    // Radius 0: the only candidate tile is the spawner's own, and it is walled.
    const ItemTypeRegistry& items = world.item_types();
    world.map().set_object(TilePos{4, 4, 0}, tiles::kWall,
                           items.get(tiles::kWall).blocks_walk());
    REQUIRE(create_spawners(world, {{TilePos{4, 4, 0}, monsters::kRat, 1, 0, 1}}) ==
            1);
    run(world, rng, 5 * kSimHz);
    CHECK(live_monsters(world) == 0U);

    // Same, but the tile is free again once the actor standing there leaves.
    const NetId squatter = world.allocate_net_id();
    world.spawn_actor(squatter, TilePos{10, 10, 0}, kAppearancePlayer);
    REQUIRE(create_spawners(world,
                            {{TilePos{10, 10, 0}, monsters::kSkeleton, 1, 0, 1}}) ==
            1);
    run(world, rng, 3 * kSimHz);
    CHECK(live_monsters(world) == 0U);

    world.despawn(squatter);
    run(world, rng, 3 * kSimHz);
    CHECK(live_monsters(world) == 1U);
}

TEST_CASE("a spawner naming a class that does not exist is not created") {
    World world = open_world();
    CHECK(create_spawners(world, {{TilePos{5, 5, 0}, 4242, 2, 2, 10}}) == 0);
    CHECK(world.registry().view<CSpawner>().empty());
}

TEST_CASE("spawned mobs get the class from the world's catalogue, not the built-in") {
    // The whole point of loading assets/monsters.txt: tuning a class changes what
    // spawns. A world built with an edited catalogue must spawn the edited numbers.
    MonsterRegistry tuned;
    tuned.add(MonsterType{.id = monsters::kRat,
                          .name = "rato de laboratorio",
                          .appearance = kAppearanceRat,
                          .max_hp = 999,
                          .attack = 1,
                          .step_ticks = 60});
    World world = open_world(std::move(tuned));
    Rng rng(1);

    REQUIRE(create_spawners(world, {{TilePos{8, 8, 0}, monsters::kRat, 1, 1, 1}}) ==
            1);
    run(world, rng, 1);
    REQUIRE(live_monsters(world) == 1U);

    const entt::entity rat = *world.registry().view<CMonster>().begin();
    CHECK(world.registry().get<CHealth>(rat).max_hp == 999);
    CHECK(world.registry().get<CActor>(rat).step_ticks == 60);
}

TEST_CASE("the shipped monster catalogue parses and is slower than the player") {
    // The file the game actually reads, inline so the test needs no filesystem.
    const std::string text =
        "# comment\n"
        "class 1 rato\n"
        "  appearance 1\n"
        "  hp 14\n"
        "  attack 3\n"
        "  kind melee\n"
        "  range 1\n"
        "  step_ticks 13\n"
        "  aggro 4\n"
        "  leash 6\n"
        "  loot 306\n"
        "\n"
        "class 3 ogro\n"
        "  appearance 3\n"
        "  hp 90\n"
        "  attack 18\n"
        "  defense 6\n"
        "  range 2\n"
        "  step_ticks 30\n";

    MonsterRegistry parsed;
    std::string error;
    REQUIRE_MESSAGE(parse_monster_catalogue(text, parsed, &error), error);

    CHECK(parsed.count() == 2U);
    CHECK(parsed.get(monsters::kRat).max_hp == 14);
    CHECK(parsed.get(monsters::kRat).step_ticks == 13);
    CHECK(parsed.get(monsters::kRat).loot == tiles::kBoots);
    CHECK(parsed.get(monsters::kOgre).attack_range == 2);
    CHECK(parsed.get(monsters::kOgre).step_ticks == 30);
    // Keys left out keep the struct default, not zero.
    CHECK(parsed.get(monsters::kOgre).aggro_radius ==
          MonsterType{}.aggro_radius);

    // Every shipped class walks slower than the player: nothing outruns you.
    for (const MonsterTypeId id : default_monsters().ids()) {
        CAPTURE(id);
        CHECK(default_monsters().get(id).step_ticks > kDefaultStepTicks);
    }
}

TEST_CASE("a malformed catalogue is refused whole, not half-applied") {
    MonsterRegistry parsed;
    std::string error;

    // Unknown key: a `speed 10` that silently did nothing would be a mob that
    // keeps its old numbers while the author swears they changed them.
    CHECK_FALSE(parse_monster_catalogue("class 1 rato\n  speed 10\n", parsed,
                                        &error));
    CHECK(error.find("unknown key") != std::string::npos);

    CHECK_FALSE(parse_monster_catalogue("  hp 10\n", parsed, &error));
    CHECK_FALSE(parse_monster_catalogue("class 0 nada\n", parsed, &error));
    CHECK_FALSE(parse_monster_catalogue("class 1 x\n  step_ticks 0\n", parsed,
                                       &error));
    CHECK_FALSE(parse_monster_catalogue("class 1 x\n  hp 0\n", parsed, &error));
    CHECK_FALSE(parse_monster_catalogue("class 1 x\n  kind flying\n", parsed,
                                       &error));
    CHECK_FALSE(parse_monster_catalogue("# nothing but a comment\n", parsed,
                                       &error));

    // None of those left anything behind.
    CHECK(parsed.count() == 0U);

    // A good file after a bad one still works.
    REQUIRE(parse_monster_catalogue("class 2 esqueleto\n  hp 40\n", parsed,
                                    &error));
    CHECK(parsed.count() == 1U);
}

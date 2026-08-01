#include <doctest/doctest.h>

#include "sim/loot.hpp"
#include "sim/monster_io.hpp"
#include "sim/monster_type.hpp"
#include "sim/rng.hpp"
#include "sim/tile_ids.hpp"

using namespace sim;

TEST_CASE("roll_monster_loot: 0% never drops, 100% always drops") {
    MonsterType spec;
    spec.loot_table = {
        {tiles::kBoots, 1, 0},
        {tiles::kShield, 1, kLootChanceScale},
    };
    Rng rng{42};
    for (int i = 0; i < 40; ++i) {
        const auto drops = roll_monster_loot(spec, rng);
        REQUIRE(drops.size() == 1U);
        CHECK(drops[0].id == tiles::kShield);
        CHECK(drops[0].count == 1);
    }
}

TEST_CASE("roll_monster_loot: two rows roll independently") {
    MonsterType spec;
    // Both always: two stacks.
    spec.loot_table = {
        {tiles::kBoots, 1, kLootChanceScale},
        {tiles::kShield, 1, kLootChanceScale},
    };
    Rng rng{7};
    const auto drops = roll_monster_loot(spec, rng);
    REQUIRE(drops.size() == 2U);
    CHECK(drops[0].id == tiles::kBoots);
    CHECK(drops[1].id == tiles::kShield);
}

TEST_CASE("roll_monster_loot: quantity is in 1..max_count") {
    MonsterType spec;
    spec.loot_table = {{tiles::kBoots, 5, kLootChanceScale}};
    Rng rng{99};
    bool saw_below_max = false;
    for (int i = 0; i < 80; ++i) {
        const auto drops = roll_monster_loot(spec, rng);
        REQUIRE(drops.size() == 1U);
        CHECK(drops[0].count >= 1);
        CHECK(drops[0].count <= 5);
        if (drops[0].count < 5) {
            saw_below_max = true;
        }
    }
    CHECK(saw_below_max);
}

TEST_CASE("monster catalogue parses multi-arg loot lines") {
    MonsterRegistry parsed;
    std::string error;
    REQUIRE(parse_monster_catalogue(
        "class 1 rat\n"
        "  hp 10\n"
        "  step_ticks 12\n"
        "  loot 306\n"
        "  loot 302 3 500000\n",
        parsed, &error));
    const MonsterType& rat = parsed.get(1);
    REQUIRE(rat.loot_table.size() == 2U);
    CHECK(rat.loot_table[0].item == tiles::kBoots);
    CHECK(rat.loot_table[0].max_count == 1);
    CHECK(rat.loot_table[0].chance == kLootChanceScale);
    CHECK(rat.loot_table[1].item == tiles::kShield);
    CHECK(rat.loot_table[1].max_count == 3);
    CHECK(rat.loot_table[1].chance == 500000U);
}

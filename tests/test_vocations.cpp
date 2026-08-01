#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "sim/tile_ids.hpp"
#include "sim/vocation_io.hpp"
#include "sim/vocation_type.hpp"

using namespace sim;

TEST_CASE("default_vocations has the six Grimhold ids") {
    const VocationRegistry& reg = default_vocations();
    CHECK(reg.count() == 6U);
    CHECK(reg.contains(vocations::kKnight));
    CHECK(reg.contains(vocations::kPaladin));
    CHECK(reg.contains(vocations::kMage));
    CHECK(reg.contains(vocations::kDruid));
    CHECK(reg.contains(vocations::kRogue));
    CHECK(reg.contains(vocations::kNecro));
    CHECK(reg.get(vocations::kKnight).preferred_kind == AttackKind::Melee);
    CHECK(reg.get(vocations::kPaladin).preferred_kind == AttackKind::Ranged);
    CHECK(reg.get(vocations::kKnight).starter_items.size() == 2U);
}

TEST_CASE("parse_vocation_catalogue reads a block") {
    VocationRegistry parsed;
    std::string error;
    REQUIRE(parse_vocation_catalogue(
        "vocation 1 Cavaleiro\n"
        "  code CAV\n"
        "  base_hp 185\n"
        "  hp_per_level 15\n"
        "  kind melee\n"
        "  starter 300 302\n",
        parsed, &error));
    const VocationType& v = parsed.get(1);
    CHECK(v.name == "Cavaleiro");
    CHECK(v.code == "CAV");
    CHECK(v.base_hp == 185);
    CHECK(v.hp_per_level == 15);
    CHECK(v.preferred_kind == AttackKind::Melee);
    REQUIRE(v.starter_items.size() == 2U);
    CHECK(v.starter_items[0] == tiles::kSword);
    CHECK(v.starter_items[1] == tiles::kShield);
}

TEST_CASE("shipped vocations.txt matches default_vocations ids and kits") {
    std::ifstream in(GAME_ASSET_DIR "vocations.txt");
    REQUIRE(in);
    std::ostringstream buf;
    buf << in.rdbuf();
    VocationRegistry parsed;
    std::string error;
    REQUIRE(parse_vocation_catalogue(buf.str(), parsed, &error));

    const VocationRegistry& fallback = default_vocations();
    CHECK(parsed.count() == fallback.count());
    for (const VocationId id : fallback.ids()) {
        REQUIRE(parsed.contains(id));
        CHECK(parsed.get(id).name == fallback.get(id).name);
        CHECK(parsed.get(id).code == fallback.get(id).code);
        CHECK(parsed.get(id).base_hp == fallback.get(id).base_hp);
        CHECK(parsed.get(id).starter_items == fallback.get(id).starter_items);
    }
}

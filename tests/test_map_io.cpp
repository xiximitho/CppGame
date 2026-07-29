#include <doctest/doctest.h>

#include <string>

#include "sim/item_type.hpp"
#include "sim/map_io.hpp"
#include "sim/tile_ids.hpp"

using namespace sim;

TEST_CASE("parse_text_map builds a map with derived blocking and spawn") {
    const ItemTypeRegistry items = build_default_registry();
    const std::string text =
        "# tiny\n"
        "size 5 3 1\n"
        "legend . 3\n"
        "legend # 3 100\n"
        "legend ~ 4\n"
        "legend @ 3\n"
        "spawn @\n"
        "floor 0\n"
        "#####\n"
        "#.@~#\n"
        "#####\n";

    std::string err;
    const auto parsed = parse_text_map(text, items, &err);
    REQUIRE_MESSAGE(parsed.has_value(), err);

    CHECK(parsed->map.width() == 5);
    CHECK(parsed->map.height() == 3);
    CHECK(parsed->map.floors() == 1);

    CHECK_FALSE(parsed->map.is_walkable(TilePos{0, 0, 0}));  // wall
    CHECK(parsed->map.is_walkable(TilePos{1, 1, 0}));        // stone floor
    CHECK(parsed->map.is_walkable(TilePos{2, 1, 0}));        // spawn is stone
    CHECK_FALSE(parsed->map.is_walkable(TilePos{3, 1, 0}));  // water blocks

    // Blocking on the wall came from the item catalogue, not the file.
    CHECK(parsed->map.at(TilePos{0, 0, 0}).object == tiles::kWall);
    CHECK(parsed->map.at(TilePos{0, 0, 0}).blocking);

    REQUIRE(parsed->spawn.has_value());
    CHECK(parsed->spawn->x == 2);
    CHECK(parsed->spawn->y == 1);
    CHECK(parsed->spawn->z == 0);
}

TEST_CASE("void cells and short rows are unwalkable holes") {
    const ItemTypeRegistry items = build_default_registry();
    const std::string text =
        "size 4 2 1\n"
        "legend . 3\n"
        "floor 0\n"
        "..\n"      // short row: columns 2 and 3 are void
        "....\n";

    const auto parsed = parse_text_map(text, items);
    REQUIRE(parsed.has_value());
    CHECK(parsed->map.is_walkable(TilePos{0, 0, 0}));
    CHECK_FALSE(parsed->map.is_walkable(TilePos{3, 0, 0}));  // void
    CHECK(parsed->map.is_walkable(TilePos{3, 1, 0}));
    CHECK_FALSE(parsed->spawn.has_value());
}

TEST_CASE("malformed maps are rejected with a reason") {
    const ItemTypeRegistry items = build_default_registry();
    std::string err;

    // grid before any size
    CHECK_FALSE(parse_text_map("legend . 3\nfloor 0\n", items, &err).has_value());
    CHECK_FALSE(err.empty());

    // a grid char with no legend entry
    CHECK_FALSE(
        parse_text_map("size 2 1 1\nlegend . 3\nfloor 0\n.?\n", items, &err)
            .has_value());

    // grid ends before all rows are read
    CHECK_FALSE(
        parse_text_map("size 3 3 1\nlegend . 3\nfloor 0\n...\n", items, &err)
            .has_value());
}

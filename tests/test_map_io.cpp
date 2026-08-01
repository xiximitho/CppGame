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

TEST_CASE("write_text_map round-trips through parse") {
    const ItemTypeRegistry items = build_default_registry();
    const std::string src =
        "size 4 3 1\n"
        "legend . 3\n"
        "legend # 3 100\n"
        "legend ~ 4\n"
        "legend @ 3\n"
        "spawn @\n"
        "floor 0\n"
        "####\n"
        "#.@~\n"
        "####\n";

    const auto first = parse_text_map(src, items);
    REQUIRE(first.has_value());

    const std::string text = write_text_map(first->map, first->spawn);
    const auto second = parse_text_map(text, items);
    REQUIRE_MESSAGE(second.has_value(), text);

    CHECK(second->map.width() == first->map.width());
    CHECK(second->map.height() == first->map.height());
    CHECK(second->map.floors() == first->map.floors());

    for (int y = 0; y < first->map.height(); ++y) {
        for (int x = 0; x < first->map.width(); ++x) {
            const TilePos p{static_cast<std::int16_t>(x),
                            static_cast<std::int16_t>(y), 0};
            CAPTURE(x);
            CAPTURE(y);
            CHECK(second->map.is_walkable(p) == first->map.is_walkable(p));
            CHECK(second->map.at(p).object == first->map.at(p).object);
        }
    }

    REQUIRE(second->spawn.has_value());
    CHECK(second->spawn->x == first->spawn->x);
    CHECK(second->spawn->y == first->spawn->y);
}

TEST_CASE("portal lines are read, and round-trip through the writer") {
    const ItemTypeRegistry items = build_default_registry();
    const std::string src =
        "size 6 3 2\n"
        "legend . 3\n"
        "legend # 3 100\n"
        "portal 1 1 0 4 1 1\n"
        "portal 4 1 1 1 1 0\n"  // the return trip: the normal way to author one
        "floor 0\n"
        "######\n"
        "#....#\n"
        "######\n"
        "floor 1\n"
        "######\n"
        "#....#\n"
        "######\n";

    std::string err;
    const auto first = parse_text_map(src, items, &err);
    REQUIRE_MESSAGE(first.has_value(), err);
    REQUIRE(first->portals.size() == 2);
    CHECK(first->portals[0].from == TilePos{1, 1, 0});
    CHECK(first->portals[0].to == TilePos{4, 1, 1});
    CHECK(first->portals[1].from == TilePos{4, 1, 1});
    CHECK(first->portals[1].to == TilePos{1, 1, 0});

    // The editor cannot author portals yet but must not drop them on save.
    const std::string text = write_text_map(first->map, first->spawn,
                                            first->monsters, first->spawners,
                                            first->portals);
    const auto second = parse_text_map(text, items, &err);
    REQUIRE_MESSAGE(second.has_value(), err);
    REQUIRE(second->portals.size() == first->portals.size());
    for (std::size_t i = 0; i < first->portals.size(); ++i) {
        CAPTURE(i);
        CHECK(second->portals[i].from == first->portals[i].from);
        CHECK(second->portals[i].to == first->portals[i].to);
    }
}

TEST_CASE("a portal an author typed wrong is a parse error, not a dead tile") {
    const ItemTypeRegistry items = build_default_registry();
    // One walkable strip, walls around it, and a rock tile at 4,1.
    const auto with_portal = [&](const std::string& line) {
        return parse_text_map("size 6 3 1\n"
                              "legend . 3\n"
                              "legend # 3 100\n" +
                                  line +
                                  "floor 0\n"
                                  "######\n"
                                  "#..##\n"
                                  "######\n",
                              items);
    };

    // Sanity: the same map with a good portal parses.
    CHECK(with_portal("portal 1 1 0 2 1 0\n").has_value());

    // Off the map, either end.
    CHECK_FALSE(with_portal("portal 1 1 0 40 1 0\n").has_value());
    CHECK_FALSE(with_portal("portal 1 1 9 2 1 0\n").has_value());
    // Leads to itself: only ever a no-op, so it is a typo by definition.
    CHECK_FALSE(with_portal("portal 1 1 0 1 1 0\n").has_value());
    // Into rock. The runtime refuses this in silence, which is indistinguishable
    // from a broken portal for whoever is standing on it — so it fails here.
    CHECK_FALSE(with_portal("portal 1 1 0 4 1 0\n").has_value());
    // FROM rock: a portal that can never fire. A live-looking dead line.
    CHECK_FALSE(with_portal("portal 4 1 0 1 1 0\n").has_value());
    // Missing fields.
    CHECK_FALSE(with_portal("portal 1 1 0 2 1\n").has_value());

    // Order does not matter: a portal may be written before the grid that fills
    // those tiles in, so walkability cannot be judged when the line is read.
    std::string err;
    const auto parsed = parse_text_map("size 6 3 1\n"
                                       "portal 1 1 0 2 1 0\n"
                                       "legend . 3\n"
                                       "legend # 3 100\n"
                                       "floor 0\n"
                                       "######\n"
                                       "#..###\n"
                                       "######\n",
                                       items, &err);
    REQUIRE_MESSAGE(parsed.has_value(), err);
    CHECK(parsed->portals.size() == 1);
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

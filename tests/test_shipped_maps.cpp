// The maps that ship in assets/maps, checked as content rather than as code.
//
// These are the only tests that read the repository. They exist because of one
// failure mode that nothing else catches: a stair painted on a tile with no floor
// to lead to. sim::World::apply_tile_transition refuses such a step in silence —
// which is the right behaviour, and is indistinguishable from "stairs are broken"
// when you are the one walking onto it. A parse succeeds, a screenshot looks right,
// and the map is simply missing the thing it was drawn for.
//
// A portal cannot fail this way: a bad `portal` line fails the parse outright, so a
// map with one never loads. The round-trip case below still checks portals, for the
// day the editor can author them.

#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "sim/components.hpp"
#include "sim/item_type.hpp"
#include "sim/map_io.hpp"
#include "sim/systems.hpp"
#include "sim/tile_map.hpp"
#include "sim/world.hpp"

using namespace sim;

namespace {

/// Every map the repository ships. Hardcoded rather than globbed: a map that stops
/// being listed here should be a visible edit, not a silently skipped file.
constexpr const char* kMaps[] = {"caverna.txt",  "dungeon.txt", "floresta.txt",
                                 "ilha.txt",     "torre.txt",   "vila.txt"};

std::string read_map(const char* name) {
    const std::string path = std::string(GAME_ASSET_DIR) + "maps/" + name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot open " << path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

TEST_CASE("every shipped map parses") {
    const ItemTypeRegistry items = build_default_registry();
    for (const char* name : kMaps) {
        std::string error;
        const auto parsed = parse_text_map(read_map(name), items, &error);
        REQUIRE_MESSAGE(parsed.has_value(), name << ": " << error);
        CHECK(parsed->map.width() > 0);
        CHECK(parsed->map.floors() > 0);
    }
}

TEST_CASE("every authored stair leads somewhere walkable") {
    const ItemTypeRegistry items = build_default_registry();
    for (const char* name : kMaps) {
        const auto parsed = parse_text_map(read_map(name), items, nullptr);
        REQUIRE(parsed.has_value());
        const TileMap& map = parsed->map;

        for (int z = 0; z < map.floors(); ++z) {
            for (int y = 0; y < map.height(); ++y) {
                for (int x = 0; x < map.width(); ++x) {
                    const TilePos at{static_cast<std::int16_t>(x),
                                     static_cast<std::int16_t>(y),
                                     static_cast<std::int8_t>(z)};
                    const int delta = items.get(map.at(at).object).stair_delta_z();
                    if (delta == 0) {
                        continue;
                    }
                    const TilePos to{at.x, at.y,
                                     static_cast<std::int8_t>(at.z + delta)};
                    CHECK_MESSAGE(map.is_walkable(to),
                                  name << ": stair at " << x << "," << y << ","
                                       << z << " leads to a tile that cannot be "
                                          "stood on");
                }
            }
        }
    }
}

TEST_CASE("saving a shipped map from the editor changes nothing") {
    // What pressing S in the editor does, without the editor: the same parse and the
    // same writer. Worth its own test because the editor is the tool people open a
    // multi-floor map in, and a writer that lost a floor — or a legend glyph, or the
    // stairs on it — would quietly rewrite the map it was asked to touch.
    const ItemTypeRegistry items = build_default_registry();
    for (const char* name : kMaps) {
        const auto first = parse_text_map(read_map(name), items, nullptr);
        REQUIRE(first.has_value());
        const std::string written = write_text_map(first->map, first->spawn,
                                                   first->monsters,
                                                   first->spawners,
                                                   first->portals);
        std::string error;
        const auto again = parse_text_map(written, items, &error);
        REQUIRE_MESSAGE(again.has_value(), name << ": " << error);

        REQUIRE(again->map.width() == first->map.width());
        REQUIRE(again->map.height() == first->map.height());
        REQUIRE_MESSAGE(again->map.floors() == first->map.floors(),
                        name << ": floor count changed on save");
        CHECK(again->spawn == first->spawn);
        CHECK(again->monsters.size() == first->monsters.size());
        CHECK(again->spawners.size() == first->spawners.size());
        // No shipped map has a portal yet. The check is here so the day one does,
        // a save that dropped it fails the suite instead of the map.
        REQUIRE(again->portals.size() == first->portals.size());
        for (std::size_t i = 0; i < first->portals.size(); ++i) {
            CHECK(again->portals[i].from == first->portals[i].from);
            CHECK(again->portals[i].to == first->portals[i].to);
        }

        bool identical = true;
        for (int z = 0; z < first->map.floors() && identical; ++z) {
            for (int y = 0; y < first->map.height() && identical; ++y) {
                for (int x = 0; x < first->map.width() && identical; ++x) {
                    const TilePos at{static_cast<std::int16_t>(x),
                                     static_cast<std::int16_t>(y),
                                     static_cast<std::int8_t>(z)};
                    const Tile& a = first->map.at(at);
                    const Tile& b = again->map.at(at);
                    identical = a.ground == b.ground && a.object == b.object &&
                                a.blocking == b.blocking;
                }
            }
        }
        CHECK_MESSAGE(identical, name << ": a tile changed on save");
    }
}

TEST_CASE("the tower's stairs can be walked, from its spawn") {
    const ItemTypeRegistry items = build_default_registry();
    auto parsed = parse_text_map(read_map("torre.txt"), items, nullptr);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->spawn.has_value());
    REQUIRE(parsed->map.floors() == 3);

    // The first up-stair on the ground floor, found rather than hardcoded: the map
    // is generated content and its coordinates are free to move.
    TilePos stair{};
    bool    found = false;
    for (int y = 0; y < parsed->map.height() && !found; ++y) {
        for (int x = 0; x < parsed->map.width() && !found; ++x) {
            const TilePos at{static_cast<std::int16_t>(x),
                             static_cast<std::int16_t>(y), 0};
            if (items.get(parsed->map.at(at).object).stair_delta_z() > 0) {
                stair = at;
                found = true;
            }
        }
    }
    REQUIRE(found);

    const TilePos spawn = *parsed->spawn;
    World world(std::move(parsed->map), items);
    const NetId        id = world.allocate_net_id();
    const entt::entity actor = world.spawn_actor(id, spawn, 0);

    // Walking there is the whole point: a stair only fires on arrival by step, so a
    // test that placed the actor on it would prove nothing about playing the map.
    REQUIRE(world.request_move_to(id, stair));

    // Generous: the route is ~20 tiles at 9 ticks each, and the cap is only here so
    // a broken follower fails the test instead of hanging it.
    for (int i = 0; i < 1000; ++i) {
        world.step();
        update_path_followers(world);
        if (world.registry().get<CPosition>(actor).tile.z != 0) {
            break;
        }
    }
    CHECK(world.registry().get<CPosition>(actor).tile ==
          TilePos{stair.x, stair.y, 1});
}

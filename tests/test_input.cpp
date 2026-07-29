#include <doctest/doctest.h>

#include <initializer_list>

#include "client/input.hpp"
#include "client/iso.hpp"

using client::input::direction_towards;
using client::input::Scheme;
using client::input::ScreenDir;
using client::input::to_grid;

TEST_CASE("screen-relative keys map to the directions that look right") {
    // Pressing W must move the character visually upward. On a 2:1 isometric
    // projection that is grid NorthWest, which is the whole subtlety this mapping
    // exists to absorb.
    CHECK(to_grid(ScreenDir::Up, Scheme::ScreenRelative) ==
          sim::Direction::NorthWest);
    CHECK(to_grid(ScreenDir::Right, Scheme::ScreenRelative) ==
          sim::Direction::NorthEast);
    CHECK(to_grid(ScreenDir::Down, Scheme::ScreenRelative) ==
          sim::Direction::SouthEast);
    CHECK(to_grid(ScreenDir::Left, Scheme::ScreenRelative) ==
          sim::Direction::SouthWest);

    CHECK(to_grid(ScreenDir::UpRight, Scheme::ScreenRelative) ==
          sim::Direction::North);
    CHECK(to_grid(ScreenDir::DownRight, Scheme::ScreenRelative) ==
          sim::Direction::East);
    CHECK(to_grid(ScreenDir::DownLeft, Scheme::ScreenRelative) ==
          sim::Direction::South);
    CHECK(to_grid(ScreenDir::UpLeft, Scheme::ScreenRelative) ==
          sim::Direction::West);
}

TEST_CASE("the screen-relative mapping actually moves the right way on screen") {
    // Verified against the projection itself rather than against a table, so this
    // still holds if the tile aspect ratio changes.
    const client::iso::ScreenPos origin = client::iso::tile_to_screen(0.0F, 0.0F, 0);

    struct Expectation {
        ScreenDir dir;
        int       expect_x;  // -1, 0 or 1
        int       expect_y;
    };
    const Expectation cases[] = {
        {ScreenDir::Up, 0, -1},   {ScreenDir::Down, 0, 1},
        {ScreenDir::Left, -1, 0}, {ScreenDir::Right, 1, 0},
    };

    for (const Expectation& expectation : cases) {
        const sim::Direction grid =
            to_grid(expectation.dir, Scheme::ScreenRelative);
        const sim::TileDelta delta = sim::direction_delta(grid);
        const client::iso::ScreenPos moved = client::iso::tile_to_screen(
            static_cast<float>(delta.dx), static_cast<float>(delta.dy), 0);

        const float dx = moved.x - origin.x;
        const float dy = moved.y - origin.y;

        if (expectation.expect_x == 0) {
            CHECK(dx == doctest::Approx(0.0F));
        } else {
            CHECK(dx * static_cast<float>(expectation.expect_x) > 0.0F);
        }
        if (expectation.expect_y == 0) {
            CHECK(dy == doctest::Approx(0.0F));
        } else {
            CHECK(dy * static_cast<float>(expectation.expect_y) > 0.0F);
        }
    }
}

TEST_CASE("grid-aligned keys map straight onto the grid axes") {
    CHECK(to_grid(ScreenDir::Up, Scheme::GridAligned) == sim::Direction::North);
    CHECK(to_grid(ScreenDir::Right, Scheme::GridAligned) == sim::Direction::East);
    CHECK(to_grid(ScreenDir::Down, Scheme::GridAligned) == sim::Direction::South);
    CHECK(to_grid(ScreenDir::Left, Scheme::GridAligned) == sim::Direction::West);
}

TEST_CASE("both schemes are bijections over the eight directions") {
    for (const Scheme scheme : {Scheme::ScreenRelative, Scheme::GridAligned}) {
        bool seen[8] = {};
        for (int i = 0; i < 8; ++i) {
            const sim::Direction grid = to_grid(static_cast<ScreenDir>(i), scheme);
            const auto index = static_cast<std::size_t>(grid);
            REQUIRE(index < 8);
            CHECK_FALSE(seen[index]);
            seen[index] = true;
        }
        for (const bool used : seen) {
            CHECK(used);
        }
    }
}

TEST_CASE("direction_towards picks the step that closes the gap") {
    const sim::TilePos from{10, 10, 0};

    CHECK(direction_towards(from, sim::TilePos{15, 10, 0}) == sim::Direction::East);
    CHECK(direction_towards(from, sim::TilePos{5, 10, 0}) == sim::Direction::West);
    CHECK(direction_towards(from, sim::TilePos{10, 4, 0}) == sim::Direction::North);
    CHECK(direction_towards(from, sim::TilePos{10, 40, 0}) == sim::Direction::South);

    CHECK(direction_towards(from, sim::TilePos{20, 20, 0}) ==
          sim::Direction::SouthEast);
    CHECK(direction_towards(from, sim::TilePos{0, 0, 0}) ==
          sim::Direction::NorthWest);
    CHECK(direction_towards(from, sim::TilePos{20, 0, 0}) ==
          sim::Direction::NorthEast);
    CHECK(direction_towards(from, sim::TilePos{0, 20, 0}) ==
          sim::Direction::SouthWest);
}

TEST_CASE("direction_towards on the current tile is harmless") {
    const sim::TilePos here{7, 7, 0};
    // No crash, no undefined direction; standing still is the sensible answer.
    CHECK(direction_towards(here, here) == sim::Direction::South);
}

TEST_CASE("a step in the chosen direction really reduces the distance") {
    const sim::TilePos from{10, 10, 0};
    const sim::TilePos targets[] = {
        {14, 10, 0}, {10, 14, 0}, {6, 6, 0}, {14, 6, 0}, {6, 14, 0}, {13, 12, 0},
    };

    for (const sim::TilePos& target : targets) {
        const sim::Direction dir = direction_towards(from, target);
        const sim::TilePos next = sim::tile_step(from, dir);
        CHECK(sim::tile_distance(next, target) < sim::tile_distance(from, target));
    }
}

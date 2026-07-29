#include <doctest/doctest.h>

#include <initializer_list>

#include "client/input.hpp"
#include "client/iso.hpp"

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

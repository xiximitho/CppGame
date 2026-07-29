#include <doctest/doctest.h>

#include "client/iso.hpp"

using client::iso::depth_key;
using client::iso::Layer;
using client::iso::screen_to_tile;
using client::iso::tile_to_screen;

TEST_CASE("projection and picking are exact inverses at tile centres") {
    // The single most load-bearing pair of functions in the renderer: if these
    // disagree, clicks land on the wrong tile and nothing else can be trusted.
    for (int z = 0; z < 4; ++z) {
        for (int y = -20; y <= 40; ++y) {
            for (int x = -20; x <= 40; ++x) {
                const sim::TilePos original{static_cast<std::int16_t>(x),
                                            static_cast<std::int16_t>(y),
                                            static_cast<std::int8_t>(z)};

                const client::iso::ScreenPos apex = tile_to_screen(original);
                // Sample the middle of the diamond, not its shared top vertex.
                const sim::TilePos round_tripped = screen_to_tile(
                    apex.x, apex.y + static_cast<float>(client::iso::kHalfTileHeight),
                    z);

                CHECK(round_tripped.x == original.x);
                CHECK(round_tripped.y == original.y);
                CHECK(round_tripped.z == original.z);
            }
        }
    }
}

TEST_CASE("grid NorthWest is straight up on screen") {
    // This is the fact the whole input mapping depends on. If the projection
    // constants ever change, this test is what catches it.
    const client::iso::ScreenPos origin = tile_to_screen(0.0F, 0.0F, 0);
    const client::iso::ScreenPos north_west = tile_to_screen(-1.0F, -1.0F, 0);

    CHECK(north_west.x == doctest::Approx(origin.x));
    CHECK(north_west.y < origin.y);

    const client::iso::ScreenPos north_east = tile_to_screen(1.0F, -1.0F, 0);
    CHECK(north_east.y == doctest::Approx(origin.y));
    CHECK(north_east.x > origin.x);

    const client::iso::ScreenPos south_east = tile_to_screen(1.0F, 1.0F, 0);
    CHECK(south_east.x == doctest::Approx(origin.x));
    CHECK(south_east.y > origin.y);
}

TEST_CASE("a tile occupies exactly one tile width and height") {
    const client::iso::ScreenPos a = tile_to_screen(0.0F, 0.0F, 0);
    const client::iso::ScreenPos b = tile_to_screen(1.0F, 0.0F, 0);

    CHECK(b.x - a.x == doctest::Approx(client::iso::kHalfTileWidth));
    CHECK(b.y - a.y == doctest::Approx(client::iso::kHalfTileHeight));
}

TEST_CASE("higher floors are drawn further up the screen") {
    const client::iso::ScreenPos ground = tile_to_screen(5.0F, 5.0F, 0);
    const client::iso::ScreenPos upper = tile_to_screen(5.0F, 5.0F, 1);

    CHECK(upper.y == doctest::Approx(ground.y - client::iso::kFloorHeight));
    CHECK(upper.x == doctest::Approx(ground.x));
}

TEST_CASE("depth ordering is back to front, then by layer, with floors dominating") {
    // Nearer to the camera means larger tile_x + tile_y.
    CHECK(depth_key(0.0F, 0.0F, 0, Layer::Ground) <
          depth_key(1.0F, 0.0F, 0, Layer::Ground));
    CHECK(depth_key(4.0F, 4.0F, 0, Layer::Ground) <
          depth_key(5.0F, 4.0F, 0, Layer::Ground));

    // Within one tile: ground, then objects, then actors.
    CHECK(depth_key(3.0F, 3.0F, 0, Layer::Ground) <
          depth_key(3.0F, 3.0F, 0, Layer::Object));
    CHECK(depth_key(3.0F, 3.0F, 0, Layer::Object) <
          depth_key(3.0F, 3.0F, 0, Layer::Actor));

    // A floor above always draws after everything below it, however far away.
    CHECK(depth_key(500.0F, 500.0F, 0, Layer::Actor) <
          depth_key(0.0F, 0.0F, 1, Layer::Ground));
}

TEST_CASE("picking respects the floor offset") {
    // The same screen point resolves to different tiles depending on which floor
    // is being tested against, which is what makes clicking on an upper storey
    // work.
    const client::iso::ScreenPos apex = tile_to_screen(10.0F, 10.0F, 1);
    const float sample_y = apex.y + static_cast<float>(client::iso::kHalfTileHeight);

    const sim::TilePos on_upper = screen_to_tile(apex.x, sample_y, 1);
    CHECK(on_upper.x == 10);
    CHECK(on_upper.y == 10);

    const sim::TilePos on_ground = screen_to_tile(apex.x, sample_y, 0);
    CHECK((on_ground.x != 10 || on_ground.y != 10));
}

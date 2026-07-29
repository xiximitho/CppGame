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

TEST_CASE("objects and actors sort back to front, then by layer") {
    // Nearer to the camera means larger tile_x + tile_y.
    CHECK(depth_key(0.0F, 0.0F, 0, Layer::Object) <
          depth_key(1.0F, 0.0F, 0, Layer::Object));
    CHECK(depth_key(4.0F, 4.0F, 0, Layer::Actor) <
          depth_key(5.0F, 4.0F, 0, Layer::Actor));

    // Within one tile: objects, then actors.
    CHECK(depth_key(3.0F, 3.0F, 0, Layer::Object) <
          depth_key(3.0F, 3.0F, 0, Layer::Actor));

    // A floor above always draws after everything below it, however far away.
    CHECK(depth_key(500.0F, 500.0F, 0, Layer::Actor) <
          depth_key(0.0F, 0.0F, 1, Layer::Ground));
}

TEST_CASE("ground is a flat layer and never sorts positionally") {
    // Every ground tile of a floor shares one key: a flat floor tile cannot stand
    // in front of anything on a neighbouring tile, so it must not compete.
    CHECK(depth_key(0.0F, 0.0F, 0, Layer::Ground) ==
          depth_key(50.0F, 90.0F, 0, Layer::Ground));

    // And it is below everything else on its floor, no matter how far back that
    // object or actor is.
    CHECK(depth_key(999.0F, 999.0F, 0, Layer::Ground) <
          depth_key(0.0F, 0.0F, 0, Layer::Object));
    CHECK(depth_key(999.0F, 999.0F, 0, Layer::Ground) <
          depth_key(0.0F, 0.0F, 0, Layer::Actor));

    // Ground of the floor above still covers objects of the floor below.
    CHECK(depth_key(999.0F, 999.0F, 0, Layer::Actor) <
          depth_key(0.0F, 0.0F, 1, Layer::Ground));
}

TEST_CASE("an actor mid-step is never covered by the tile it walks into") {
    // Regression. Ground used to sort by position, so an actor halfway from (10,10)
    // to (11,10) keyed at 2052 while the ground of (11,10) keyed at 2100 — the
    // destination tile drew over the sprite and its diamond edge sliced visibly
    // through the character for the entire step.
    for (int step = 0; step <= 10; ++step) {
        const float progress = static_cast<float>(step) / 10.0F;

        for (int i = 0; i < 8; ++i) {
            const sim::TileDelta delta =
                sim::direction_delta(static_cast<sim::Direction>(i));

            const float from_x = 10.0F;
            const float from_y = 10.0F;
            const float at_x = from_x + static_cast<float>(delta.dx) * progress;
            const float at_y = from_y + static_cast<float>(delta.dy) * progress;

            const float actor = depth_key(at_x, at_y, 0, Layer::Actor);

            // Neither the tile being left nor the one being entered may cover it.
            CHECK(actor > depth_key(from_x, from_y, 0, Layer::Ground));
            CHECK(actor > depth_key(from_x + static_cast<float>(delta.dx),
                                    from_y + static_cast<float>(delta.dy), 0,
                                    Layer::Ground));
        }
    }
}

TEST_CASE("floor decals sit above ground but below anything standing on it") {
    // world_render draws the cursor highlight at Ground + 0.5; that band has to
    // stay clear of the object/actor range.
    const float highlight = depth_key(5.0F, 5.0F, 0, Layer::Ground) + 0.5F;

    CHECK(highlight > depth_key(80.0F, 80.0F, 0, Layer::Ground));
    CHECK(highlight < depth_key(0.0F, 0.0F, 0, Layer::Object));
    CHECK(highlight < depth_key(0.0F, 0.0F, 0, Layer::Actor));
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

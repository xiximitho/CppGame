#include <doctest/doctest.h>

#include "client/animation.hpp"
#include "client/iso.hpp"

using client::anim::art_direction;
using client::anim::kArtDirsFull;
using client::anim::kArtDirsTibia;
using client::anim::walk_frame;

TEST_CASE("a static set never leaves frame 0") {
    // The point of this case: nothing downstream has to know whether a mob is
    // animated. A set with one frame answers 0 whatever the actor is doing, so the
    // render path has no branch for it.
    CHECK(walk_frame(false, 0, 1) == 0);
    CHECK(walk_frame(true, 0, 1) == 0);
    CHECK(walk_frame(true, 128, 1) == 0);
    CHECK(walk_frame(true, 255, 1) == 0);
    // Zero frames is what an appearance with no art at all reports; it must not
    // divide by anything or index anything.
    CHECK(walk_frame(true, 255, 0) == 0);
}

TEST_CASE("standing still is the first frame") {
    // Whatever progress says. An actor that stopped mid-step keeps a stale
    // walk_progress in the snapshot, and freezing on a leg-forward pose would look
    // like the animation had crashed.
    CHECK(walk_frame(false, 0, 3) == 0);
    CHECK(walk_frame(false, 200, 3) == 0);
}

TEST_CASE("the walk cycle runs once across one step") {
    // Three frames, evenly: the whole cycle plays between leaving one tile and
    // arriving at the next, which is what ties the animation to the step instead of
    // to a clock.
    CHECK(walk_frame(true, 0, 3) == 0);
    CHECK(walk_frame(true, 85, 3) == 0);
    CHECK(walk_frame(true, 86, 3) == 1);
    CHECK(walk_frame(true, 170, 3) == 1);
    CHECK(walk_frame(true, 171, 3) == 2);
    CHECK(walk_frame(true, 255, 3) == 2);
}

TEST_CASE("no progress value can index past the cycle") {
    // walk_frame is what picks the array slot in Tileset::actor, so this is a bounds
    // guarantee, not a cosmetic one. Every frame count, every progress byte.
    for (int frames = 1; frames <= client::anim::kMaxFrames; ++frames) {
        for (int progress = 0; progress <= 255; ++progress) {
            const int frame =
                walk_frame(true, static_cast<std::uint8_t>(progress),
                           static_cast<std::uint8_t>(frames));
            CHECK(frame >= 0);
            CHECK(frame < frames);
        }
    }
}

TEST_CASE("an eight-direction set uses the grid direction as-is") {
    for (int d = 0; d < sim::kDirectionCount; ++d) {
        const auto dir = static_cast<sim::Direction>(d);
        CHECK(art_direction(dir, kArtDirsFull) == d);
    }
}

TEST_CASE("four-direction art: the diagonals are the screen axes") {
    // Tibia art is drawn in SCREEN space — back, right, front, left — and in the 2:1
    // projection it is the grid diagonals that point along the screen axes. Getting
    // this backwards is the classic bug: the mob faces 45 degrees off from where it
    // is walking, consistently, in a way that looks like bad art rather than a bad
    // table.
    CHECK(art_direction(sim::Direction::NorthWest, kArtDirsTibia) == 0);  // up
    CHECK(art_direction(sim::Direction::NorthEast, kArtDirsTibia) == 1);  // right
    CHECK(art_direction(sim::Direction::SouthEast, kArtDirsTibia) == 2);  // down
    CHECK(art_direction(sim::Direction::SouthWest, kArtDirsTibia) == 3);  // left
}

TEST_CASE("four-direction art: every grid direction has a column, two per column") {
    int used[kArtDirsTibia] = {0, 0, 0, 0};
    for (int d = 0; d < sim::kDirectionCount; ++d) {
        const int art = art_direction(static_cast<sim::Direction>(d), kArtDirsTibia);
        REQUIRE(art >= 0);
        REQUIRE(art < kArtDirsTibia);
        ++used[art];
    }
    // Evenly: each art direction covers one diagonal plus one cardinal. An uneven
    // split would mean some facing had been rounded twice and one column was dead.
    for (const int count : used) {
        CHECK(count == 2);
    }
}

TEST_CASE("the chosen column agrees with where the actor moves on screen") {
    // Checked against the projection itself, like test_input.cpp does, rather than
    // against the table above: if the tile aspect ratio ever changes, this fails
    // instead of quietly picking the wrong sprite.
    //
    // The rule being verified is "the art direction's screen axis must not disagree
    // with the movement": a mob moving rightwards on screen may be drawn facing
    // right, up or down, but never left.
    const client::iso::ScreenPos origin = client::iso::tile_to_screen(0.0F, 0.0F, 0);

    // Screen axis of each art column: 0 up, 1 right, 2 down, 3 left.
    const int axis_x[kArtDirsTibia] = {0, 1, 0, -1};
    const int axis_y[kArtDirsTibia] = {-1, 0, 1, 0};

    for (int d = 0; d < sim::kDirectionCount; ++d) {
        const auto dir = static_cast<sim::Direction>(d);
        const sim::TileDelta delta = sim::direction_delta(dir);
        const client::iso::ScreenPos moved = client::iso::tile_to_screen(
            static_cast<float>(delta.dx), static_cast<float>(delta.dy), 0);
        const float dx = moved.x - origin.x;
        const float dy = moved.y - origin.y;

        const int art = art_direction(dir, kArtDirsTibia);
        // Not "points the same way" — four columns cannot cover eight directions —
        // but "does not point the opposite way" on either axis.
        CHECK(dx * static_cast<float>(axis_x[art]) >= 0.0F);
        CHECK(dy * static_cast<float>(axis_y[art]) >= 0.0F);
    }
}

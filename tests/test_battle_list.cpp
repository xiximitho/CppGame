#include <doctest/doctest.h>

#include "client/battle_list.hpp"

using client::build_battle_list;
using client::WorldView;

namespace {

sim::ActorState actor(sim::NetId id, int x, int y, int z = 0,
                      std::uint16_t appearance = 1) {
    sim::ActorState state;
    state.net_id = id;
    state.tile = sim::TilePos{static_cast<std::int16_t>(x),
                              static_cast<std::int16_t>(y),
                              static_cast<std::int8_t>(z)};
    state.appearance = appearance;
    state.hp = 20;
    state.max_hp = 20;
    return state;
}

WorldView view_with(std::vector<sim::ActorState> actors, sim::NetId local) {
    WorldView view;
    view.actors = std::move(actors);
    view.local_id = local;
    view.ready = true;
    return view;
}

}  // namespace

TEST_CASE("the battle list is ordered by distance, nearest first") {
    const WorldView view = view_with(
        {
            actor(1, 10, 10, 0, sim::kAppearancePlayer),  // the local player
            actor(2, 18, 10),                             // 8 away
            actor(3, 12, 10),                             // 2 away
            actor(4, 10, 15),                             // 5 away
        },
        1);

    const auto list = build_battle_list(view);
    REQUIRE(list.size() == 3U);
    CHECK(list[0].net_id == 3);
    CHECK(list[0].distance == 2);
    CHECK(list[1].net_id == 4);
    CHECK(list[1].distance == 5);
    CHECK(list[2].net_id == 2);
    CHECK(list[2].distance == 8);
}

TEST_CASE("distance is Chebyshev, the same measure the simulation uses") {
    // A diagonal neighbour is one step away, not 1.41 of one: reach and aggro are
    // counted this way, so a battle list sorted any other way would disagree with
    // what is actually within swinging distance.
    const WorldView view = view_with({actor(1, 10, 10), actor(2, 11, 11),
                                      actor(3, 13, 10)},
                                     1);
    const auto list = build_battle_list(view);
    REQUIRE(list.size() == 2U);
    CHECK(list[0].net_id == 2);
    CHECK(list[0].distance == 1);
    CHECK(list[1].distance == 3);
}

TEST_CASE("ties are broken by id so rows do not swap under the cursor") {
    const WorldView view = view_with({actor(1, 10, 10), actor(9, 12, 10),
                                      actor(4, 10, 12), actor(7, 8, 10)},
                                     1);
    const auto list = build_battle_list(view);
    REQUIRE(list.size() == 3U);
    CHECK(list[0].net_id == 4);
    CHECK(list[1].net_id == 7);
    CHECK(list[2].net_id == 9);
    for (const auto& entry : list) {
        CHECK(entry.distance == 2);
    }
}

TEST_CASE("the local player is never in its own battle list") {
    const WorldView view = view_with({actor(1, 5, 5), actor(2, 6, 5)}, 1);
    const auto list = build_battle_list(view);
    REQUIRE(list.size() == 1U);
    CHECK(list[0].net_id == 2);
}

TEST_CASE("creatures on another floor are left out") {
    const WorldView view =
        view_with({actor(1, 5, 5, 0), actor(2, 6, 5, 1), actor(3, 7, 5, 0)}, 1);
    const auto list = build_battle_list(view);
    REQUIRE(list.size() == 1U);
    CHECK(list[0].net_id == 3);
}

TEST_CASE("an unready view or a missing local actor lists nothing") {
    WorldView not_ready = view_with({actor(1, 5, 5), actor(2, 6, 5)}, 1);
    not_ready.ready = false;
    CHECK(build_battle_list(not_ready).empty());

    // Between connecting and the first snapshot the local id is not in the list yet;
    // measuring distance from nothing would be measuring from (0,0).
    const WorldView no_local = view_with({actor(2, 6, 5), actor(3, 7, 5)}, 1);
    CHECK(build_battle_list(no_local).empty());
}

TEST_CASE("the list carries what a row needs to draw itself") {
    WorldView view = view_with({actor(1, 5, 5), actor(2, 8, 5, 0, 3)}, 1);
    view.actors[1].hp = 45;
    view.actors[1].max_hp = 90;

    const auto list = build_battle_list(view);
    REQUIRE(list.size() == 1U);
    CHECK(list[0].appearance == 3);  // which sprite, and which class name
    CHECK(list[0].hp == 45);
    CHECK(list[0].max_hp == 90);
}

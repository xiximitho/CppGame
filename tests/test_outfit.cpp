#include <doctest/doctest.h>

#include "sim/components.hpp"
#include "sim/outfit.hpp"
#include "sim/snapshot.hpp"
#include "sim/spell.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/world.hpp"

using namespace sim;

namespace {

World make_world() {
    TileMap map(12, 12, 1);
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                   static_cast<std::int16_t>(y), 0},
                           tiles::kStone);
        }
    }
    return World(std::move(map), build_default_registry());
}

}  // namespace

TEST_CASE("outfit palette index clamps out of range") {
    CHECK(clamp_outfit_index(0) == 0);
    CHECK(clamp_outfit_index(15) == 15);
    CHECK(clamp_outfit_index(16) == 0);
    CHECK(clamp_outfit_index(255) == 0);

    COutfit outfit;
    outfit.set(OutfitLayer::Head, 99);
    CHECK(outfit.head == 0);
    outfit.set(OutfitLayer::Body, 7);
    CHECK(outfit.body == 7);
    CHECK(outfit.index(OutfitLayer::Body) == 7);
}

TEST_CASE("apply_vocation plus outfit lands on the actor and the snapshot") {
    World world = make_world();
    const entt::entity e = world.spawn_actor(1, TilePos{4, 4, 0}, 0);
    apply_vocation(world, e, vocations::kKnight);

    const COutfit look{2, 8, 11, 5};
    world.registry().emplace_or_replace<COutfit>(e, look);

    const ActorState state = read_actor_state(world, e);
    CHECK(state.appearance == 0);
    CHECK(state.outfit.head == 2);
    CHECK(state.outfit.body == 8);
    CHECK(state.outfit.legs == 11);
    CHECK(state.outfit.feet == 5);
    CHECK(state.hp > 0);
}

TEST_CASE("outfit colour table has sixteen named entries") {
    CHECK(kOutfitPaletteSize == 16);
    CHECK(outfit_color(0).name != nullptr);
    CHECK(outfit_color(0).name[0] != '\0');
    CHECK(outfit_color(15).r == 20);
}

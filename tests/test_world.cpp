#include <doctest/doctest.h>

#include "sim/components.hpp"
#include "sim/snapshot.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/world.hpp"

namespace {

/// An open floor with no obstacles, so each test adds only the geometry it cares
/// about.
sim::TileMap open_map(int width = 32, int height = 32, int floors = 2) {
    sim::TileMap map(width, height, floors);
    for (int z = 0; z < floors; ++z) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                map.set_ground(sim::TilePos{static_cast<std::int16_t>(x),
                                            static_cast<std::int16_t>(y),
                                            static_cast<std::int8_t>(z)},
                               sim::tiles::kGrass);
            }
        }
    }
    return map;
}

void block(sim::TileMap& map, int x, int y, int z = 0) {
    map.set_object(sim::TilePos{static_cast<std::int16_t>(x),
                                static_cast<std::int16_t>(y),
                                static_cast<std::int8_t>(z)},
                   sim::tiles::kWall, true);
}

void advance(sim::World& world, sim::Tick ticks) {
    for (sim::Tick i = 0; i < ticks; ++i) {
        world.step();
    }
}

/// The tick order the server and SoloSession both use: advance time, then let
/// route followers issue their next step.
void advance_with_systems(sim::World& world, sim::Tick ticks) {
    for (sim::Tick i = 0; i < ticks; ++i) {
        world.step();
        sim::update_path_followers(world);
    }
}

sim::TilePos tile_of(const sim::World& world, sim::NetId id) {
    return world.registry().get<sim::CPosition>(world.lookup(id)).tile;
}

}  // namespace

TEST_CASE("a step takes exactly the configured number of ticks") {
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);

    REQUIRE(world.request_walk(id, sim::Direction::East));

    const entt::entity entity = world.lookup(id);
    // Extra parentheses stop doctest from decomposing the expression: its
    // Expression_lhs wrapper is ambiguous against entt::null_t's own operators.
    REQUIRE((entity != entt::null));

    // Still in transit one tick before arrival.
    advance(world, sim::kDefaultStepTicks - 1);
    CHECK(world.registry().all_of<sim::CWalk>(entity));
    CHECK(world.registry().get<sim::CPosition>(entity).tile ==
          sim::TilePos{10, 10, 0});

    advance(world, 1);
    CHECK_FALSE(world.registry().all_of<sim::CWalk>(entity));
    CHECK(world.registry().get<sim::CPosition>(entity).tile ==
          sim::TilePos{11, 10, 0});
}

TEST_CASE("diagonal steps take longer than cardinal ones") {
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);

    REQUIRE(world.request_walk(id, sim::Direction::SouthEast));

    const entt::entity entity = world.lookup(id);
    const auto& walk = world.registry().get<sim::CWalk>(entity);
    CHECK(walk.end_tick - walk.start_tick ==
          sim::step_ticks_for_diagonal(sim::kDefaultStepTicks));
    CHECK(walk.end_tick - walk.start_tick > sim::kDefaultStepTicks);
}

TEST_CASE("input during a step is dropped, not queued") {
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);

    REQUIRE(world.request_walk(id, sim::Direction::East));
    CHECK_FALSE(world.request_walk(id, sim::Direction::East));

    advance(world, sim::kDefaultStepTicks);
    CHECK(world.registry().get<sim::CPosition>(world.lookup(id)).tile ==
          sim::TilePos{11, 10, 0});
}

TEST_CASE("walking into a wall fails but still turns the actor") {
    sim::TileMap map = open_map();
    block(map, 11, 10);
    sim::World world(std::move(map));

    const sim::NetId id = world.allocate_net_id();
    const entt::entity entity =
        world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);

    CHECK_FALSE(world.request_walk(id, sim::Direction::East));
    // Turning on a blocked move is what makes walking into a wall feel
    // responsive rather than ignored.
    CHECK(world.registry().get<sim::CPosition>(entity).facing ==
          sim::Direction::East);
    CHECK_FALSE(world.registry().all_of<sim::CWalk>(entity));
}

TEST_CASE("a tile with no ground is a hole, not a floor") {
    sim::TileMap map = open_map();
    map.set_ground(sim::TilePos{11, 10, 0}, sim::kTileEmpty);
    sim::World world(std::move(map));

    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);

    CHECK_FALSE(world.request_walk(id, sim::Direction::East));
}

TEST_CASE("two actors cannot step into the same tile") {
    sim::World world(open_map());

    const sim::NetId left = world.allocate_net_id();
    const sim::NetId right = world.allocate_net_id();
    world.spawn_actor(left, sim::TilePos{10, 10, 0}, 0);
    world.spawn_actor(right, sim::TilePos{12, 10, 0}, 0);

    // Both aim at (11,10). The destination is claimed at step start, so the
    // second request must fail immediately rather than being discovered on
    // arrival.
    REQUIRE(world.request_walk(left, sim::Direction::East));
    CHECK_FALSE(world.request_walk(right, sim::Direction::West));
}

TEST_CASE("a vacated tile becomes available as soon as the step starts") {
    sim::World world(open_map());

    const sim::NetId first = world.allocate_net_id();
    const sim::NetId second = world.allocate_net_id();
    world.spawn_actor(first, sim::TilePos{10, 10, 0}, 0);
    world.spawn_actor(second, sim::TilePos{9, 10, 0}, 0);

    CHECK(world.occupant(sim::TilePos{10, 10, 0}) == first);
    REQUIRE(world.request_walk(first, sim::Direction::East));

    CHECK(world.occupant(sim::TilePos{11, 10, 0}) == first);
    CHECK(world.occupant(sim::TilePos{10, 10, 0}) == sim::kInvalidNetId);
    CHECK(world.request_walk(second, sim::Direction::East));
}

TEST_CASE("diagonal moves cannot slip through the corner of two walls") {
    sim::TileMap map = open_map();
    // Walls east and south of the actor; the south-east diagonal must be refused
    // even though the destination tile itself is clear.
    block(map, 11, 10);
    block(map, 10, 11);
    sim::World world(std::move(map));

    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);

    CHECK_FALSE(world.request_walk(id, sim::Direction::SouthEast));
}

TEST_CASE("a single wall beside a diagonal is enough to refuse it") {
    // The strict rule: BOTH tiles orthogonally adjacent to a diagonal must be
    // walkable. The lenient alternative (either one is enough) lets an actor clip
    // visibly through the corner of a solid wall block, so strict is the right
    // default for this art style. Relaxing it is a one-line change in
    // World::can_enter, but it is a gameplay decision, not a detail.
    sim::TileMap map = open_map();
    block(map, 11, 10);
    sim::World world(std::move(map));

    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);

    CHECK_FALSE(world.request_walk(id, sim::Direction::SouthEast));
    // The other diagonal on the clear side is still fine.
    CHECK(world.request_walk(id, sim::Direction::NorthWest));
}

TEST_CASE("a diagonal across open ground is allowed") {
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);

    CHECK(world.request_walk(id, sim::Direction::SouthEast));
    advance(world, sim::step_ticks_for_diagonal(sim::kDefaultStepTicks));
    CHECK(world.registry().get<sim::CPosition>(world.lookup(id)).tile ==
          sim::TilePos{11, 11, 0});
}

TEST_CASE("despawning releases both the current and the destination tile") {
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);
    REQUIRE(world.request_walk(id, sim::Direction::East));

    world.despawn(id);

    CHECK(world.occupant(sim::TilePos{10, 10, 0}) == sim::kInvalidNetId);
    CHECK(world.occupant(sim::TilePos{11, 10, 0}) == sim::kInvalidNetId);
    CHECK((world.lookup(id) == entt::null));
}

TEST_CASE("actors on other floors are independent") {
    sim::World world(open_map());
    const sim::NetId ground = world.allocate_net_id();
    const sim::NetId upper = world.allocate_net_id();
    world.spawn_actor(ground, sim::TilePos{10, 10, 0}, 0);
    world.spawn_actor(upper, sim::TilePos{10, 10, 1}, 0);

    // Same x/y, different floor: neither blocks the other.
    CHECK(world.occupant(sim::TilePos{10, 10, 0}) == ground);
    CHECK(world.occupant(sim::TilePos{10, 10, 1}) == upper);
    CHECK(world.request_walk(ground, sim::Direction::East));
    CHECK(world.request_walk(upper, sim::Direction::East));
}

TEST_CASE("the area of interest excludes distant actors and other floors") {
    sim::World world(open_map(128, 128, 2));

    const sim::NetId centre = world.allocate_net_id();
    world.spawn_actor(centre, sim::TilePos{64, 64, 0}, 0);

    const sim::NetId near_by = world.allocate_net_id();
    world.spawn_actor(near_by, sim::TilePos{66, 66, 0}, 0);

    const sim::NetId far_away = world.allocate_net_id();
    world.spawn_actor(far_away,
                      sim::TilePos{static_cast<std::int16_t>(64 + sim::kAoiHalfX + 1),
                                   64, 0},
                      0);

    const sim::NetId other_floor = world.allocate_net_id();
    world.spawn_actor(other_floor, sim::TilePos{64, 64, 1}, 0);

    sim::Snapshot snapshot;
    sim::build_snapshot(world, sim::TilePos{64, 64, 0}, snapshot);

    CHECK(snapshot.actors.size() == 2);

    bool saw_centre = false;
    bool saw_near = false;
    for (const sim::ActorState& actor : snapshot.actors) {
        saw_centre = saw_centre || actor.net_id == centre;
        saw_near = saw_near || actor.net_id == near_by;
    }
    CHECK(saw_centre);
    CHECK(saw_near);
}

TEST_CASE("a snapshot describes a step's progress, not an interpolated position") {
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{10, 10, 0}, 0);
    REQUIRE(world.request_walk(id, sim::Direction::East));

    // Halfway through the step.
    advance(world, sim::kDefaultStepTicks / 2);

    sim::Snapshot snapshot;
    sim::build_snapshot(world, sim::TilePos{10, 10, 0}, snapshot);
    REQUIRE(snapshot.actors.size() == 1);

    const sim::ActorState& actor = snapshot.actors[0];
    CHECK(actor.walking);
    CHECK(actor.walk_dir == sim::Direction::East);
    // The tile reported is the one being left, so a client that missed earlier
    // snapshots still knows where the step began.
    CHECK(actor.tile == sim::TilePos{10, 10, 0});
    CHECK(actor.walk_progress > 0);
    CHECK(actor.walk_progress < 255);

    const sim::InterpolatedPos pos = sim::interpolate(actor);
    CHECK(pos.x > 10.0F);
    CHECK(pos.x < 11.0F);
    CHECK(pos.y == doctest::Approx(10.0F));
}

TEST_CASE("a standing actor interpolates to its exact tile") {
    sim::ActorState actor;
    actor.tile = sim::TilePos{7, 3, 1};
    actor.walking = false;

    const sim::InterpolatedPos pos = sim::interpolate(actor);
    CHECK(pos.x == doctest::Approx(7.0F));
    CHECK(pos.y == doctest::Approx(3.0F));
    CHECK(pos.z == 1);
}

TEST_CASE("an actor asked to move to a tile actually arrives there") {
    // The behaviour click-to-move exists for: naming a destination and getting
    // there, rather than taking a single step in its general direction.
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{5, 5, 0}, 0);

    const sim::TilePos target{14, 11, 0};
    REQUIRE(world.request_move_to(id, target));
    CHECK(world.is_following_path(id));

    advance_with_systems(world, 400);

    CHECK(tile_of(world, id) == target);
    // The route is dropped on arrival, not left dangling.
    CHECK_FALSE(world.is_following_path(id));
}

TEST_CASE("a route walks around a wall instead of into it") {
    sim::TileMap map = open_map();
    for (int y = 0; y < 32; ++y) {
        if (y != 2) {
            block(map, 10, y);
        }
    }
    sim::World world(std::move(map));

    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{5, 20, 0}, 0);

    const sim::TilePos target{15, 20, 0};
    REQUIRE(world.request_move_to(id, target));

    advance_with_systems(world, 2000);
    CHECK(tile_of(world, id) == target);
}

TEST_CASE("moving to an unreachable or unwalkable tile is refused up front") {
    sim::TileMap map = open_map();
    block(map, 9, 9);
    sim::World world(std::move(map));

    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{5, 5, 0}, 0);

    CHECK_FALSE(world.request_move_to(id, sim::TilePos{9, 9, 0}));
    CHECK_FALSE(world.is_following_path(id));

    // Another floor: there are no stairs yet.
    CHECK_FALSE(world.request_move_to(id, sim::TilePos{6, 6, 1}));
}

TEST_CASE("manual input cancels an active route") {
    // Pressing a direction key while auto-walking has to take back control,
    // otherwise the two fight each other every tick.
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{5, 5, 0}, 0);

    REQUIRE(world.request_move_to(id, sim::TilePos{20, 5, 0}));
    advance_with_systems(world, 20);
    REQUIRE(world.is_following_path(id));

    world.cancel_path(id);
    CHECK_FALSE(world.is_following_path(id));

    // The step already in flight completes; the actor is not yanked backwards.
    const sim::TilePos before = tile_of(world, id);
    advance_with_systems(world, 200);
    const sim::TilePos after = tile_of(world, id);
    CHECK(after.x >= before.x);
    CHECK(after.x <= before.x + 1);
}

TEST_CASE("requesting a new route replaces the old one") {
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{5, 5, 0}, 0);

    REQUIRE(world.request_move_to(id, sim::TilePos{20, 5, 0}));
    advance_with_systems(world, 30);

    const sim::TilePos second_target{5, 15, 0};
    REQUIRE(world.request_move_to(id, second_target));
    advance_with_systems(world, 600);

    CHECK(tile_of(world, id) == second_target);
}

TEST_CASE("a follower permanently blocked by another actor gives up") {
    // Routes are planned ignoring actors, so a corridor with someone standing in it
    // produces a step that is refused every tick. It must abandon the route instead
    // of shoving forever.
    sim::TileMap map(32, 3, 1);
    for (int x = 0; x < 32; ++x) {
        map.set_ground(sim::TilePos{static_cast<std::int16_t>(x), 1, 0},
                       sim::tiles::kGrass);
    }
    sim::World world(std::move(map));

    const sim::NetId walker = world.allocate_net_id();
    const sim::NetId blocker = world.allocate_net_id();
    world.spawn_actor(walker, sim::TilePos{5, 1, 0}, 0);
    world.spawn_actor(blocker, sim::TilePos{7, 1, 0}, 0);

    REQUIRE(world.request_move_to(walker, sim::TilePos{20, 1, 0}));

    advance_with_systems(world, sim::kDefaultStepTicks + sim::kPathBlockedGiveUpTicks + 10);

    CHECK_FALSE(world.is_following_path(walker));
    // It got as far as the tile before the blocker and stopped.
    CHECK(tile_of(world, walker) == sim::TilePos{6, 1, 0});
    CHECK(tile_of(world, blocker) == sim::TilePos{7, 1, 0});
}

TEST_CASE("a route requested mid-step is planned from the tile being entered") {
    // Planning from the tile being left would put the tile the actor is already
    // walking onto at the head of the route, making it visibly step backwards.
    sim::World world(open_map());
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{5, 5, 0}, 0);

    REQUIRE(world.request_walk(id, sim::Direction::East));
    advance(world, 2);  // mid-step, heading for (6,5)

    const sim::TilePos target{10, 5, 0};
    REQUIRE(world.request_move_to(id, target));

    advance_with_systems(world, 400);
    CHECK(tile_of(world, id) == target);
}

TEST_CASE("walking off the edge of the world is refused") {
    sim::World world(open_map(8, 8, 1));
    const sim::NetId id = world.allocate_net_id();
    world.spawn_actor(id, sim::TilePos{0, 0, 0}, 0);

    CHECK_FALSE(world.request_walk(id, sim::Direction::West));
    CHECK_FALSE(world.request_walk(id, sim::Direction::North));
    CHECK(world.request_walk(id, sim::Direction::East));
}

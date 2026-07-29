#include <doctest/doctest.h>

#include <vector>

#include "sim/pathfind.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"

namespace {

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

/// The invariant that ties the pathfinder to the movement rules: every step of a
/// returned route must be one the simulation will actually accept. If these two
/// ever disagree, an actor stalls forever on a step its own path told it to take.
void check_path_is_walkable(const sim::TileMap& map, sim::TilePos from,
                            const std::vector<sim::TilePos>& path) {
    sim::TilePos current = from;
    for (const sim::TilePos& next : path) {
        sim::Direction dir = sim::Direction::South;
        REQUIRE(sim::direction_between(current, next, dir));
        CHECK(sim::can_traverse(map, current, dir));
        current = next;
    }
}

int path_cost(sim::TilePos from, const std::vector<sim::TilePos>& path) {
    int cost = 0;
    sim::TilePos current = from;
    for (const sim::TilePos& next : path) {
        sim::Direction dir = sim::Direction::South;
        if (!sim::direction_between(current, next, dir)) {
            return -1;
        }
        cost += sim::is_diagonal(dir) ? sim::kPathCostDiagonal
                                      : sim::kPathCostCardinal;
        current = next;
    }
    return cost;
}

}  // namespace

TEST_CASE("a clear route is found and ends on the target") {
    const sim::TileMap map = open_map();
    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    const sim::TilePos from{5, 5, 0};
    const sim::TilePos to{12, 5, 0};

    REQUIRE(finder.find(map, from, to, path));
    CHECK(path.back() == to);
    // The starting tile is excluded: the actor is already standing on it.
    CHECK(path.front() != from);
    CHECK(path.size() == 7);
    check_path_is_walkable(map, from, path);
}

TEST_CASE("the route prefers diagonals when they arrive sooner") {
    const sim::TileMap map = open_map();
    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    const sim::TilePos from{5, 5, 0};
    const sim::TilePos to{10, 10, 0};

    REQUIRE(finder.find(map, from, to, path));

    // Five diagonals at 13 ticks each beats ten cardinals at 9.
    CHECK(path.size() == 5);
    CHECK(path_cost(from, path) == 5 * sim::kPathCostDiagonal);
}

TEST_CASE("costs are in ticks, so the fastest route wins rather than the shortest") {
    // A pure tile count would rate 5 diagonals (5 tiles) against 10 cardinals
    // (10 tiles) and pick the diagonals for the wrong reason. Check the costs
    // actually reflect arrival time.
    CHECK(sim::kPathCostCardinal == static_cast<int>(sim::kDefaultStepTicks));
    CHECK(sim::kPathCostDiagonal ==
          static_cast<int>(sim::step_ticks_for_diagonal(sim::kDefaultStepTicks)));
    CHECK(sim::kPathCostDiagonal < 2 * sim::kPathCostCardinal);
}

TEST_CASE("the route goes around a wall") {
    sim::TileMap map = open_map();
    // A vertical wall with a gap at y = 10.
    for (int y = 0; y < 32; ++y) {
        if (y != 10) {
            block(map, 8, y);
        }
    }
    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    const sim::TilePos from{5, 20, 0};
    const sim::TilePos to{12, 20, 0};

    REQUIRE(finder.find(map, from, to, path));
    check_path_is_walkable(map, from, path);
    CHECK(path.back() == to);

    // It must actually use the gap.
    bool through_gap = false;
    for (const sim::TilePos& tile : path) {
        if (tile.x == 8 && tile.y == 10) {
            through_gap = true;
        }
    }
    CHECK(through_gap);
}

TEST_CASE("no route to an unwalkable target") {
    sim::TileMap map = open_map();
    block(map, 12, 5);

    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    CHECK_FALSE(finder.find(map, sim::TilePos{5, 5, 0}, sim::TilePos{12, 5, 0}, path));
    CHECK(path.empty());
}

TEST_CASE("no route into a sealed room") {
    sim::TileMap map = open_map();
    // Seal (20,20) completely.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dy != 0) {
                block(map, 20 + dx, 20 + dy);
            }
        }
    }

    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    CHECK_FALSE(
        finder.find(map, sim::TilePos{5, 5, 0}, sim::TilePos{20, 20, 0}, path));
    CHECK(path.empty());
    // Bounded work, not a search of the whole map for every rejected click.
    CHECK(finder.last_expanded() <= sim::kPathMaxNodes);
}

TEST_CASE("the route never clips a wall corner diagonally") {
    sim::TileMap map = open_map();
    // A diagonal staircase of walls. Without the corner rule, A* would happily
    // thread between them, and the movement code would then refuse every step.
    for (int i = 0; i < 10; ++i) {
        block(map, 10 + i, 10 + i);
        block(map, 11 + i, 10 + i);
    }

    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    if (finder.find(map, sim::TilePos{5, 15, 0}, sim::TilePos{25, 12, 0}, path)) {
        check_path_is_walkable(map, sim::TilePos{5, 15, 0}, path);
    }
}

TEST_CASE("degenerate requests are refused") {
    const sim::TileMap map = open_map();
    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    // Already there.
    CHECK_FALSE(finder.find(map, sim::TilePos{5, 5, 0}, sim::TilePos{5, 5, 0}, path));
    // Another floor: there are no stairs, so cross-floor routing is meaningless.
    CHECK_FALSE(finder.find(map, sim::TilePos{5, 5, 0}, sim::TilePos{5, 5, 1}, path));
    // Off the map.
    CHECK_FALSE(
        finder.find(map, sim::TilePos{5, 5, 0}, sim::TilePos{999, 999, 0}, path));
    CHECK_FALSE(
        finder.find(map, sim::TilePos{-5, -5, 0}, sim::TilePos{5, 5, 0}, path));
}

TEST_CASE("the node budget bounds the search") {
    const sim::TileMap map = open_map(64, 64, 1);
    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    // A budget too small to cross the map must fail rather than run to completion.
    CHECK_FALSE(finder.find(map, sim::TilePos{1, 1, 0}, sim::TilePos{62, 62, 0},
                            path, 10));
    CHECK(path.empty());

    // The same request succeeds with a real budget, proving the failure above was
    // the budget and not a broken map.
    CHECK(finder.find(map, sim::TilePos{1, 1, 0}, sim::TilePos{62, 62, 0}, path));
}

TEST_CASE("one Pathfinder serves many calls correctly") {
    // It keeps scratch buffers between calls and uses a generation stamp instead of
    // clearing them. A stale stamp would make the second search read the first
    // one's costs.
    const sim::TileMap map = open_map();
    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    for (int i = 0; i < 25; ++i) {
        const auto x = static_cast<std::int16_t>(5 + (i % 15));
        const sim::TilePos from{x, 5, 0};
        const sim::TilePos to{static_cast<std::int16_t>(x + 5), 12, 0};

        REQUIRE(finder.find(map, from, to, path));
        CHECK(path.back() == to);
        check_path_is_walkable(map, from, path);
    }
}

TEST_CASE("the heuristic never overestimates the real cost") {
    // Admissibility. If this breaks, A* silently stops returning optimal routes.
    const sim::TileMap map = open_map();
    sim::Pathfinder finder;
    std::vector<sim::TilePos> path;

    const sim::TilePos from{3, 3, 0};
    for (int y = 3; y < 20; ++y) {
        for (int x = 3; x < 20; ++x) {
            const sim::TilePos to{static_cast<std::int16_t>(x),
                                  static_cast<std::int16_t>(y), 0};
            if (to == from) {
                continue;
            }
            REQUIRE(finder.find(map, from, to, path));
            CHECK(sim::path_heuristic(from, to) <= path_cost(from, path));
        }
    }
}

#pragma once

#include <cstdint>
#include <vector>

#include "sim/tile_map.hpp"
#include "sim/types.hpp"

namespace sim {

/// Step costs, in simulation ticks, so the search optimises for arrival *time*
/// rather than tile count. A diagonal covers more ground and costs proportionally
/// more, exactly matching step_ticks_for_diagonal — otherwise the "shortest" path
/// would be the slower one.
constexpr int kPathCostCardinal = static_cast<int>(kDefaultStepTicks);
constexpr int kPathCostDiagonal =
    static_cast<int>(step_ticks_for_diagonal(kDefaultStepTicks));

/// Upper bound on expanded nodes. A request that needs more is treated as
/// unreachable. This is what stops a click on an unreachable island from searching
/// the whole world every time someone spams the mouse.
constexpr int kPathMaxNodes = 6000;

/// A* over the tile grid, on a single floor.
///
/// Reusable on purpose: it keeps its scratch buffers between calls, because a
/// server pathfinding for many actors every tick should not allocate a
/// map-sized working set each time. Not thread safe — one instance per user.
class Pathfinder {
public:
    /// Fills `out_path` with the tiles to walk, excluding `from` and including
    /// `to`. Returns false when there is no route, when the target is not walkable,
    /// when the floors differ, or when the node budget is exhausted; `out_path` is
    /// cleared either way.
    ///
    /// Ignores other actors. They move, so planning around them produces detours
    /// that are stale by the time they are walked; transient blocking is handled
    /// when the step is actually attempted. Static geometry only, via can_traverse.
    bool find(const TileMap& map, TilePos from, TilePos to,
              std::vector<TilePos>& out_path, int max_nodes = kPathMaxNodes);

    /// Nodes expanded by the last call. Useful for tuning the budget.
    int last_expanded() const { return last_expanded_; }

private:
    void prepare(const TileMap& map);
    bool visited(std::size_t index) const;
    void mark_visited(std::size_t index);

    int width_ = 0;
    int height_ = 0;

    std::vector<int>           cost_;      ///< best known cost to reach a tile
    std::vector<std::int32_t>  came_from_; ///< predecessor index, -1 for the start
    std::vector<std::uint32_t> stamp_;     ///< generation, avoids clearing per call
    std::uint32_t              generation_ = 0;
    int                        last_expanded_ = 0;
};

/// Octile distance in the same tick units as the step costs. Never overestimates,
/// so A* stays admissible and the first path found is optimal.
constexpr int path_heuristic(TilePos a, TilePos b) {
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    const int diagonal = dx < dy ? dx : dy;
    const int straight = (dx > dy ? dx : dy) - diagonal;
    return diagonal * kPathCostDiagonal + straight * kPathCostCardinal;
}

}  // namespace sim

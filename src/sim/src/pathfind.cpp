#include "sim/pathfind.hpp"

#include <algorithm>
#include <queue>
#include <utility>

namespace sim {
namespace {

/// Min-heap entry: (f-score, tile index). Compared so the smallest f pops first.
struct OpenNode {
    int         f_score;
    std::size_t index;

    friend bool operator>(const OpenNode& a, const OpenNode& b) {
        // Tie-break on index purely so the result is deterministic; two paths of
        // equal cost must not depend on heap implementation details.
        if (a.f_score != b.f_score) {
            return a.f_score > b.f_score;
        }
        return a.index > b.index;
    }
};

}  // namespace

void Pathfinder::prepare(const TileMap& map) {
    const std::size_t needed = static_cast<std::size_t>(map.width()) *
                               static_cast<std::size_t>(map.height());

    if (width_ != map.width() || height_ != map.height() ||
        cost_.size() != needed) {
        width_ = map.width();
        height_ = map.height();
        cost_.assign(needed, 0);
        came_from_.assign(needed, -1);
        // Zero means "never visited", so the first generation must be 1.
        stamp_.assign(needed, 0);
        generation_ = 0;
    }

    ++generation_;
    if (generation_ == 0) {
        // Wrapped after 4 billion searches; the stamps are meaningless now.
        std::fill(stamp_.begin(), stamp_.end(), 0U);
        generation_ = 1;
    }
}

bool Pathfinder::visited(std::size_t index) const {
    return stamp_[index] == generation_;
}

void Pathfinder::mark_visited(std::size_t index) {
    stamp_[index] = generation_;
}

bool Pathfinder::find(const TileMap& map, TilePos from, TilePos to,
                      std::vector<TilePos>& out_path, int max_nodes) {
    out_path.clear();
    last_expanded_ = 0;

    if (from.z != to.z) {
        return false;
    }
    if (from == to) {
        return false;
    }
    if (!map.in_bounds(from) || !map.is_walkable(to)) {
        return false;
    }

    prepare(map);

    const auto to_index = [this](TilePos pos) {
        return static_cast<std::size_t>(pos.y) * static_cast<std::size_t>(width_) +
               static_cast<std::size_t>(pos.x);
    };
    const auto to_pos = [this, z = from.z](std::size_t index) {
        return TilePos{
            static_cast<std::int16_t>(index % static_cast<std::size_t>(width_)),
            static_cast<std::int16_t>(index / static_cast<std::size_t>(width_)), z};
    };

    const std::size_t start_index = to_index(from);
    const std::size_t goal_index = to_index(to);

    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<>> open;

    cost_[start_index] = 0;
    came_from_[start_index] = -1;
    mark_visited(start_index);
    open.push(OpenNode{path_heuristic(from, to), start_index});

    bool found = false;

    while (!open.empty()) {
        const OpenNode current = open.top();
        open.pop();

        if (current.index == goal_index) {
            found = true;
            break;
        }

        // A stale heap entry: a cheaper route to this tile was found after it was
        // pushed. std::priority_queue cannot decrease-key, so duplicates are pushed
        // and the outdated ones skipped here.
        const int current_cost = cost_[current.index];
        if (current.f_score > current_cost + path_heuristic(to_pos(current.index), to)) {
            continue;
        }

        if (++last_expanded_ > max_nodes) {
            return false;
        }

        const TilePos current_pos = to_pos(current.index);

        for (int i = 0; i < kDirectionCount; ++i) {
            const auto dir = static_cast<Direction>(i);
            if (!can_traverse(map, current_pos, dir)) {
                continue;
            }

            const TilePos next_pos = tile_step(current_pos, dir);
            const std::size_t next_index = to_index(next_pos);

            const int step =
                is_diagonal(dir) ? kPathCostDiagonal : kPathCostCardinal;
            const int next_cost = current_cost + step;

            if (visited(next_index) && next_cost >= cost_[next_index]) {
                continue;
            }

            mark_visited(next_index);
            cost_[next_index] = next_cost;
            came_from_[next_index] = static_cast<std::int32_t>(current.index);
            open.push(
                OpenNode{next_cost + path_heuristic(next_pos, to), next_index});
        }
    }

    if (!found) {
        return false;
    }

    // Walk the predecessors back to the start, then reverse. The start tile is left
    // out: the actor is already standing on it.
    for (std::int32_t index = static_cast<std::int32_t>(goal_index); index >= 0;
         index = came_from_[static_cast<std::size_t>(index)]) {
        if (static_cast<std::size_t>(index) == start_index) {
            break;
        }
        out_path.push_back(to_pos(static_cast<std::size_t>(index)));
    }
    std::reverse(out_path.begin(), out_path.end());

    return !out_path.empty();
}

}  // namespace sim

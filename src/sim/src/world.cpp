#include "sim/world.hpp"

#include <vector>

namespace sim {

World::World(TileMap map) : map_(std::move(map)) {}

std::uint64_t World::tile_key(TilePos pos) {
    // Bias into unsigned so negative coordinates still pack cleanly.
    const auto ux = static_cast<std::uint64_t>(static_cast<std::uint16_t>(pos.x));
    const auto uy = static_cast<std::uint64_t>(static_cast<std::uint16_t>(pos.y));
    const auto uz = static_cast<std::uint64_t>(static_cast<std::uint8_t>(pos.z));
    return (uz << 32U) | (uy << 16U) | ux;
}

void World::occupy(TilePos pos, NetId net_id) {
    occupancy_[tile_key(pos)] = net_id;
}

void World::vacate(TilePos pos, NetId net_id) {
    const auto it = occupancy_.find(tile_key(pos));
    // Guarded: a tile is only released by whoever actually holds it, so an
    // actor arriving on a tile another actor already left cannot erase it.
    if (it != occupancy_.end() && it->second == net_id) {
        occupancy_.erase(it);
    }
}

NetId World::allocate_net_id() {
    return next_net_id_++;
}

entt::entity World::spawn_actor(NetId net_id, TilePos at,
                               std::uint16_t appearance, Tick step_ticks) {
    const entt::entity entity = registry_.create();

    registry_.emplace<CPosition>(entity, CPosition{at, Direction::South});
    registry_.emplace<CActor>(entity, CActor{net_id, appearance, step_ticks});
    registry_.emplace<CHealth>(entity, CHealth{100, 100});

    by_net_id_[net_id] = entity;
    occupy(at, net_id);

    return entity;
}

void World::despawn(NetId net_id) {
    const auto it = by_net_id_.find(net_id);
    if (it == by_net_id_.end()) {
        return;
    }
    const entt::entity entity = it->second;

    if (const auto* pos = registry_.try_get<CPosition>(entity)) {
        vacate(pos->tile, net_id);
    }
    // A walking actor also holds its destination tile.
    if (const auto* walk = registry_.try_get<CWalk>(entity)) {
        vacate(walk->to, net_id);
    }

    registry_.destroy(entity);
    by_net_id_.erase(it);
}

entt::entity World::lookup(NetId net_id) const {
    const auto it = by_net_id_.find(net_id);
    return it == by_net_id_.end() ? entt::null : it->second;
}

NetId World::occupant(TilePos pos) const {
    const auto it = occupancy_.find(tile_key(pos));
    return it == occupancy_.end() ? kInvalidNetId : it->second;
}

bool World::can_enter(TilePos from, TilePos to, Direction dir,
                      NetId mover) const {
    if (!map_.is_walkable(to)) {
        return false;
    }

    const NetId other = occupant(to);
    if (other != kInvalidNetId && other != mover) {
        return false;
    }

    // Diagonal moves may not squeeze between two blocking tiles, otherwise
    // actors slip through the corners of walls — the classic tile-game bug.
    if (is_diagonal(dir)) {
        const TileDelta delta = direction_delta(dir);
        const TilePos side_x{static_cast<std::int16_t>(from.x + delta.dx), from.y,
                             from.z};
        const TilePos side_y{from.x, static_cast<std::int16_t>(from.y + delta.dy),
                             from.z};
        if (!map_.is_walkable(side_x) || !map_.is_walkable(side_y)) {
            return false;
        }
    }

    return true;
}

bool World::request_walk(NetId net_id, Direction dir) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return false;
    }

    auto* pos = registry_.try_get<CPosition>(entity);
    if (pos == nullptr) {
        return false;
    }

    // Turning happens even when the step is refused.
    pos->facing = dir;

    // Already mid-step: input during a step is dropped rather than queued. A
    // one-slot queue here is what you would add for responsive chained walking.
    if (registry_.all_of<CWalk>(entity)) {
        return false;
    }

    const TilePos target = tile_step(pos->tile, dir);
    if (!can_enter(pos->tile, target, dir, net_id)) {
        return false;
    }

    const auto* actor = registry_.try_get<CActor>(entity);
    const Tick base = actor != nullptr ? actor->step_ticks : kDefaultStepTicks;
    const Tick duration = is_diagonal(dir) ? step_ticks_for_diagonal(base) : base;

    registry_.emplace<CWalk>(entity,
                             CWalk{pos->tile, target, tick_, tick_ + duration, dir});

    // The destination is claimed immediately so a second actor cannot start a
    // step into the same tile while this one is in flight.
    vacate(pos->tile, net_id);
    occupy(target, net_id);

    return true;
}

bool World::request_turn(NetId net_id, Direction dir) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return false;
    }
    auto* pos = registry_.try_get<CPosition>(entity);
    if (pos == nullptr) {
        return false;
    }
    pos->facing = dir;
    return true;
}

void World::step() {
    ++tick_;

    // Collected first rather than removed during iteration: mutating the pool a
    // view is walking is how you get a dangling iterator in any ECS.
    std::vector<entt::entity> arrived;
    for (auto [entity, walk] : registry_.view<CWalk>().each()) {
        if (tick_ >= walk.end_tick) {
            arrived.push_back(entity);
        }
    }

    for (const entt::entity entity : arrived) {
        const auto& walk = registry_.get<CWalk>(entity);
        auto& pos = registry_.get<CPosition>(entity);
        pos.tile = walk.to;
        registry_.erase<CWalk>(entity);
    }
}

}  // namespace sim

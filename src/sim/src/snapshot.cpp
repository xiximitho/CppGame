#include "sim/snapshot.hpp"

#include "sim/components.hpp"

namespace sim {

ActorState read_actor_state(const World& world, entt::entity entity) {
    const entt::registry& registry = world.registry();

    ActorState state;

    const auto& actor = registry.get<CActor>(entity);
    state.net_id     = actor.net_id;
    state.appearance = actor.appearance;

    const auto& pos = registry.get<CPosition>(entity);
    state.tile   = pos.tile;
    state.facing = pos.facing;

    if (const auto* walk = registry.try_get<CWalk>(entity)) {
        state.walking  = true;
        state.walk_dir = walk->dir;
        state.tile     = walk->from;

        const Tick duration = walk->end_tick - walk->start_tick;
        if (duration > 0) {
            const Tick elapsed = world.tick() - walk->start_tick;
            const std::uint32_t scaled = (elapsed * 255U) / duration;
            state.walk_progress =
                static_cast<std::uint8_t>(scaled > 255U ? 255U : scaled);
        } else {
            state.walk_progress = 255;
        }
    }

    if (const auto* health = registry.try_get<CHealth>(entity)) {
        state.hp     = static_cast<std::int16_t>(health->hp);
        state.max_hp = static_cast<std::int16_t>(health->max_hp);
    }

    return state;
}

void build_snapshot(const World& world, TilePos center, Snapshot& out) {
    out.tick = world.tick();
    out.actors.clear();

    const auto view = world.registry().view<CPosition, CActor>();
    for (const entt::entity entity : view) {
        const CPosition& pos = view.get<CPosition>(entity);
        if (pos.tile.z != center.z) {
            continue;
        }
        const int dx = pos.tile.x - center.x;
        const int dy = pos.tile.y - center.y;
        if (dx < -kAoiHalfX || dx > kAoiHalfX || dy < -kAoiHalfY ||
            dy > kAoiHalfY) {
            continue;
        }
        out.actors.push_back(read_actor_state(world, entity));
    }
}

InterpolatedPos interpolate(const ActorState& state) {
    InterpolatedPos result;
    result.x = static_cast<float>(state.tile.x);
    result.y = static_cast<float>(state.tile.y);
    result.z = state.tile.z;

    if (state.walking) {
        const float t = static_cast<float>(state.walk_progress) / 255.0F;
        const TileDelta delta = direction_delta(state.walk_dir);
        result.x += static_cast<float>(delta.dx) * t;
        result.y += static_cast<float>(delta.dy) * t;
    }

    return result;
}

}  // namespace sim

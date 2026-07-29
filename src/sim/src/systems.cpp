#include "sim/systems.hpp"

#include <vector>

#include "sim/components.hpp"

namespace sim {

void update_wanderers(World& world, Rng& rng) {
    entt::registry& registry = world.registry();
    const Tick now = world.tick();

    // Decisions are gathered before any are applied: request_walk() adds a CWalk
    // component, which would invalidate a view being iterated.
    struct Decision {
        NetId     net_id;
        Direction dir;
    };
    std::vector<Decision> decisions;

    for (auto [entity, wanderer, actor] :
         registry.view<CWanderer, CActor>().each()) {
        if (now < wanderer.next_decision_tick) {
            continue;
        }
        if (registry.all_of<CWalk>(entity)) {
            continue;
        }
        const auto dir =
            static_cast<Direction>(rng.next_below(kDirectionCount));
        decisions.push_back({actor.net_id, dir});
        wanderer.next_decision_tick =
            now + static_cast<Tick>(rng.next_range(kSimHz / 2, kSimHz * 3));
    }

    for (const Decision& decision : decisions) {
        world.request_walk(decision.net_id, decision.dir);
    }
}

void update_path_followers(World& world) {
    entt::registry& registry = world.registry();

    // Two phases, as everywhere else in this file: request_walk() adds a CWalk
    // component, which would invalidate the view being iterated.
    struct Attempt {
        NetId        net_id;
        entt::entity entity;
        Direction    dir;
    };
    std::vector<Attempt>      attempts;
    std::vector<entt::entity> finished;

    const auto view = registry.view<CPathFollow, CPosition, CActor>();
    for (const entt::entity entity : view) {
        // Mid-step: nothing to do until it lands.
        if (registry.all_of<CWalk>(entity)) {
            continue;
        }

        const CPathFollow& follow = view.get<CPathFollow>(entity);
        if (follow.next >= follow.path.size()) {
            finished.push_back(entity);
            continue;
        }

        const TilePos current = view.get<CPosition>(entity).tile;
        const TilePos want = follow.path[follow.next];

        Direction dir = Direction::South;
        if (!direction_between(current, want, dir)) {
            // The actor is not adjacent to the next tile of its route, so the route
            // no longer describes where it is — teleported, shoved, or the path
            // outlived the actor's position. Drop it rather than walk nonsense.
            finished.push_back(entity);
            continue;
        }

        attempts.push_back(Attempt{view.get<CActor>(entity).net_id, entity, dir});
    }

    for (const Attempt& attempt : attempts) {
        if (world.request_walk(attempt.net_id, attempt.dir)) {
            auto& follow = registry.get<CPathFollow>(attempt.entity);
            ++follow.next;
            follow.blocked_ticks = 0;
            if (follow.next >= follow.path.size()) {
                finished.push_back(attempt.entity);
            }
            continue;
        }

        auto& follow = registry.get<CPathFollow>(attempt.entity);
        if (++follow.blocked_ticks >= kPathBlockedGiveUpTicks) {
            finished.push_back(attempt.entity);
        }
    }

    for (const entt::entity entity : finished) {
        // An entity can land here twice (arrived and then flagged again), and
        // remove() on an absent component is a no-op, so this needs no dedup.
        registry.remove<CPathFollow>(entity);
    }
}

}  // namespace sim

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

}  // namespace sim

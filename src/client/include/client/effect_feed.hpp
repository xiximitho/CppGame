#pragma once

#include <vector>

#include "client/renderer2d.hpp"
#include "client/tileset.hpp"
#include "sim/types.hpp"

namespace client {

/// Renders attack effects handed over by the session (sim::AttackEvent). Melee
/// (from == to) shows a glow that blooms and fades on the target; ranged shows a
/// projectile travelling from attacker to target. Purely cosmetic and
/// short-lived, like the damage numbers.
class EffectFeed {
public:
    void spawn(const sim::AttackEvent& event, float now_seconds);
    void render(Renderer2D& renderer, const Tileset& tileset, float now_seconds);

private:
    struct Effect {
        float        from_x = 0.0F;
        float        from_y = 0.0F;
        float        to_x = 0.0F;
        float        to_y = 0.0F;
        std::uint8_t kind = 0;
        float        start = 0.0F;
    };
    std::vector<Effect> effects_;
};

}  // namespace client

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "client/renderer2d.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"
#include "sim/types.hpp"

namespace client {

/// Floating "-N" damage numbers: client-side feedback with no extra packet. A
/// drop in an actor's hp between world views *is* a hit, so this derives the
/// numbers from state the snapshot already carries (the same reason a lost
/// snapshot costs nothing). Numbers rise and fade, drawn as a 7-segment display
/// built from the atlas's solid texel — no font needed.
class DamageFeed {
public:
    /// Detects hp drops since the last call and spawns a number over each. Call
    /// once per frame with a monotonic seconds clock.
    void observe(const WorldView& view, float now_seconds);

    /// Ages out and draws the active numbers.
    void render(Renderer2D& renderer, const Tileset& tileset, float now_seconds);

private:
    struct Popup {
        float        x = 0.0F;   ///< world-screen anchor, captured at spawn
        float        y = 0.0F;
        std::int32_t amount = 0;
        float        start = 0.0F;
    };

    std::unordered_map<sim::NetId, std::int32_t> last_hp_;
    std::vector<Popup>                           popups_;
};

}  // namespace client

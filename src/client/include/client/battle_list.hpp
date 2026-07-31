#pragma once

#include <algorithm>
#include <vector>

#include "client/renderer2d.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"
#include "sim/monster_type.hpp"
#include "sim/types.hpp"

// The battle list: every creature the client can see, nearest first, clickable.
//
// It is built from the WorldView and nothing else, which gives it Tibia's exact
// semantics for free — the view holds what the server decided this player may see
// (its area of interest, its floor), so the panel cannot list something the player
// has no business knowing about. In solo play the same is true because the solo
// session builds the same snapshot the server would send.
//
// Clicking a row is an ATTACK intent, not a movement one: the simulation closes the
// distance and keeps closing it (sim::update_chasers). The client never sends a
// destination for a fight, which is what keeps a chase from turning into a walk to
// where the target used to be.

namespace client {

/// One row. `distance` is Chebyshev tiles from the local player, the same measure
/// the simulation uses for reach and aggro.
struct BattleEntry {
    sim::NetId    net_id = sim::kInvalidNetId;
    int           distance = 0;
    std::uint16_t appearance = 0;
    std::int16_t  hp = 0;
    std::int16_t  max_hp = 0;
};

/// Every visible actor except the local player, nearest first, ties broken by id so
/// the order never flickers between frames.
///
/// Inline because it is pure — a WorldView in, a sorted list out — and that makes it
/// testable without a window, a renderer or an atlas. The drawing and hit-testing
/// below cannot be: the dummy SDL driver delivers no mouse, so that half is verified
/// by screenshot, the same limit the editor's form has.
inline std::vector<BattleEntry> build_battle_list(const WorldView& view) {
    std::vector<BattleEntry> out;
    if (!view.ready) {
        return out;
    }

    const sim::ActorState* local = nullptr;
    for (const sim::ActorState& actor : view.actors) {
        if (actor.net_id == view.local_id) {
            local = &actor;
            break;
        }
    }
    if (local == nullptr) {
        return out;   // no local actor yet: nothing to measure distance from
    }

    out.reserve(view.actors.size());
    for (const sim::ActorState& actor : view.actors) {
        if (actor.net_id == view.local_id) {
            continue;
        }
        const int distance = sim::tile_distance(local->tile, actor.tile);
        if (distance < 0) {
            continue;  // another floor; a snapshot should not carry these anyway
        }
        out.push_back(BattleEntry{actor.net_id, distance, actor.appearance,
                                  actor.hp, actor.max_hp});
    }

    std::sort(out.begin(), out.end(),
              [](const BattleEntry& a, const BattleEntry& b) {
                  return a.distance != b.distance ? a.distance < b.distance
                                                  : a.net_id < b.net_id;
              });
    return out;
}

/// Draws the panel down the right edge starting at `top_y` (window pixels), and
/// frames the row for `current_target`. `monsters` supplies the class name shown on
/// each row; a class it does not know falls back to the appearance number.
void draw_battle_list(Renderer2D& renderer, const Tileset& tileset,
                      const WorldView& view, const sim::MonsterRegistry& monsters,
                      sim::NetId current_target, float top_y);

/// Which creature a window-pixel point hits, or kInvalidNetId when the point misses
/// the panel. Uses the same geometry as draw_battle_list.
sim::NetId battle_list_hit(const Renderer2D& renderer, const Tileset& tileset,
                           const WorldView& view, float mouse_x, float mouse_y,
                           float top_y);

}  // namespace client

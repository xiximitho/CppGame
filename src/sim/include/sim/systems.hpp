#pragma once

#include "sim/rng.hpp"
#include "sim/world.hpp"

namespace sim {

/// Moves every CWanderer entity in a random direction now and then. This is a
/// placeholder so the world is not empty and so snapshot/interpolation code has
/// something other than the local player to prove itself against. Real behaviour
/// gets its own systems; this one is expected to be deleted.
void update_wanderers(World& world, Rng& rng);

/// Advances every actor that is following a route from World::request_move_to,
/// issuing one step per tile.
///
/// Must run every tick, after World::step(), so a step is issued on the same tick
/// the previous one finishes; otherwise walking a route is visibly slower than
/// holding a direction key.
///
/// Routes are planned ignoring other actors, so a step can be refused when someone
/// stands in the way. That is treated as transient: the follower waits and retries,
/// and gives up after kPathBlockedGiveUpTicks rather than pushing forever.
void update_path_followers(World& world);

/// Resolves auto-attacks and respawns. Each actor with a CTarget faces its target
/// and, when in melee range and its cooldown is up, hits it for kBaseMeleeDamage;
/// dead actors past their respawn timer come back. A stale target (gone or dead)
/// is dropped.
///
/// Must run every tick after World::step(), like update_path_followers, so a
/// swing lands on the tick its cooldown elapses.
void update_combat(World& world);

/// How long a follower tolerates being blocked before abandoning its route. One
/// second is long enough for another actor to finish crossing and short enough that
/// a permanently blocked player is not left stuck without feedback.
constexpr Tick kPathBlockedGiveUpTicks = static_cast<Tick>(kSimHz);

}  // namespace sim

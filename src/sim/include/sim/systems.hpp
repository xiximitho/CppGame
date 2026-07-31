#pragma once

#include "sim/rng.hpp"
#include "sim/world.hpp"

namespace sim {

/// Spawns one monster of `type` at `at` and returns its entity.
///
/// The class's numbers are copied into the actor's components here, which is why
/// nothing downstream needs the monster catalogue: speed lands in CActor, health
/// in CHealth, damage and reach in CCombat. Returns entt::null when the class is
/// unknown, so a map naming a class that does not exist is reportable content
/// rather than a crash.
entt::entity spawn_monster(World& world, MonsterTypeId type, TilePos at);

/// Spawns every monster a map author placed. Returns how many were placed.
///
/// Entries naming a class that does not exist, or sitting on a tile that is not
/// walkable or already taken, are skipped rather than fatal: content should not be
/// able to stop a server from booting, and the count lets the caller log the gap.
int spawn_authored_monsters(World& world, const std::vector<MonsterSpawn>& list);

/// Spawns one monster of a class picked at random from `catalogue`.
///
/// Lives here rather than in the server and the solo session because both need it
/// and the two must agree: single-player growing a different mob mix than the
/// server is exactly the drift this module boundary exists to stop.
entt::entity spawn_random_monster(World& world, Rng& rng, TilePos at);

/// Creates the spawn points an author placed. Returns how many were created;
/// entries naming a class that does not exist are skipped and counted out.
///
/// A spawner is an entity with CSpawner and no CActor: invisible, server-side, and
/// never in a snapshot. Its children appear through update_spawners.
int create_spawners(World& world, const std::vector<SpawnerSpec>& list);

/// Keeps every CSpawner's population up.
///
/// Forgets children that died, and when it is short and its timer is up, places one
/// new mob on a free walkable tile within the radius. One per timer expiry rather
/// than a full refill, so clearing a nest stays cleared for a while.
///
/// Runs every tick like the other systems; the timers make it cheap.
void update_spawners(World& world, Rng& rng);

/// Drives every CMonster: chase and attack the nearest non-monster inside the
/// aggro radius, otherwise drift around home within the leash.
///
/// "Non-monster" rather than "player" on purpose. In solo play the local actor has
/// no CPlayer (there is no peer), so keying aggro on that would give monsters
/// different behaviour in single-player than on the server — precisely the drift
/// the module boundary exists to prevent.
///
/// Must run every tick after World::step(), before update_path_followers, so a
/// route decided this tick is stepped on this tick.
void update_monsters(World& world, Rng& rng);

/// Keeps every CFollow actor walking toward its target until it is in reach.
///
/// Replans when the target has moved since the route was planned (and at worst
/// four times a second), stops the route the moment the target is within the
/// attacker's reach, and drops the chase when the target dies or vanishes. This is
/// what makes "attack that one" mean "close in and keep hitting it" instead of
/// "walk once to where it used to be".
///
/// Must run every tick after World::step() and BEFORE update_path_followers, so a
/// route planned this tick is stepped this tick.
void update_chasers(World& world);

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

/// Effective combat stats of an actor: a base plus everything equipped. The
/// weapon slot sets range and effect; unarmed is a melee punch, range 1, a
/// default glow. Pure read of CEquipment against the world's item catalogue.
struct CombatStats {
    std::int32_t attack  = kBaseMeleeDamage;
    std::int32_t defense = 0;
    std::uint8_t range   = 1;
    std::uint8_t effect  = kEffectMeleeGlow;
};
CombatStats combat_stats(const World& world, entt::entity actor);

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

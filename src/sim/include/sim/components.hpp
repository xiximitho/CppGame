#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sim/item_type.hpp"
#include "sim/monster_type.hpp"
#include "sim/types.hpp"
#include "sim/vocation_type.hpp"

namespace sim {

/// A quantity of one item type. count > 1 only for stackable types.
struct ItemStack {
    ItemTypeId    id    = kItemNone;
    std::uint16_t count = 0;
};

/// A backpack: a flat list of stacks. Kept simple (no grid) until it needs to be
/// more.
struct CInventory {
    std::vector<ItemStack> items;
};

/// Worn gear, one item id per slot (kItemNone = empty). The wearer's effective
/// attack/defense and weapon range come from summing these — see
/// sim::combat_stats.
struct CEquipment {
    std::array<ItemTypeId, kEquipSlotCount> slots{};
};

/// Where an actor stands and which way it looks. `facing` is simulation state,
/// not presentation: in a tile MMO turning is an action with its own cost, and
/// attacks and line of sight depend on it.
struct CPosition {
    TilePos   tile;
    Direction facing = Direction::South;
};

/// Present only while a step is in flight. Its absence is what "standing still"
/// means, so a walking query is a single component lookup rather than a flag
/// test, and the ECS view iterates only the actors that are actually moving.
struct CWalk {
    TilePos   from;
    TilePos   to;
    Tick      start_tick = 0;
    Tick      end_tick   = 0;
    Direction dir        = Direction::South;
};

/// Identity that survives across the wire. Every entity the client can see has
/// one; purely server-side entities (spawners, triggers) do not.
struct CActor {
    NetId         net_id     = kInvalidNetId;
    std::uint16_t appearance = 0;
    /// Ticks per cardinal step. Lower is faster.
    Tick step_ticks = kDefaultStepTicks;
};

struct CHealth {
    std::int32_t hp     = 100;
    std::int32_t max_hp = 100;
};

/// Attached to the one actor a connected player drives. `peer` is the transport
/// handle; sim/ treats it as an opaque number and never calls into net/.
struct CPlayer {
    std::uint32_t peer = 0;
    std::string   name;
    /// Last tile the map streamer sent chunks around, so the server knows when
    /// the player has moved far enough to need more of the world.
    TilePos last_streamed_center{-32767, -32767, 0};
};

/// Permanent vocation (Grimhold). Present on player actors once creation picks
/// one; absent means "legacy / not yet assigned" during the migration.
struct CVocation {
    VocationId id = kVocationNone;
};

/// Level, experience and mana. HP still lives on CHealth; max_hp/max_mana are
/// recomputed from vocation + level when XP lands (not wired yet).
struct CProgress {
    std::uint16_t level = 1;
    std::uint32_t xp    = 0;
    std::int32_t  mana  = 0;
    std::int32_t  max_mana = 0;
};

/// Next tick the actor may cast any spell. One global GCD for V1 (hotbar has one
/// spell per vocation anyway).
struct CSpellCooldown {
    Tick ready_tick = 0;
};

/// Innate combat numbers, independent of anything worn.
///
/// This is how a monster class hits harder than another without the simulation
/// needing a monster catalogue at hand: the class's numbers are copied here when
/// it spawns, and sim::combat_stats adds them to whatever gear says. Players have
/// no CCombat, so their stats come from equipment exactly as before.
struct CCombat {
    std::int16_t attack  = 0;
    std::int16_t defense = 0;
    std::uint8_t range   = 1;
    std::uint8_t effect  = kEffectMeleeGlow;
};

/// Marks an actor the server drives: which class it is, where it belongs, and who
/// it is currently after.
///
/// `home` plus `leash` is what keeps a mob in the room it was authored into — a
/// wandering monster that drifts across the map ends up in a corridor nobody
/// walks, and the map author's placement stops meaning anything.
struct CMonster {
    MonsterTypeId type = kMonsterNone;
    TilePos       home;
    std::uint8_t  aggro_radius = 6;
    std::uint8_t  leash        = 8;
    /// Throttle: AI thinks a few times a second, not 30. Chasing re-plans a route,
    /// and re-planning every tick is both wasteful and jittery.
    Tick next_decision_tick = 0;
};

/// A spawn point, alive in the world. Sits on an entity with NO CActor: it is not
/// something the client can see, so it never reaches a snapshot.
///
/// It tracks the net ids it created rather than counting nearby monsters, so a mob
/// that wandered off on a leash still counts against the population and one that
/// belongs to the spawner two rooms over does not.
struct CSpawner {
    TilePos            tile;
    MonsterTypeId      type = kMonsterNone;
    std::uint8_t       max_alive = 1;
    std::uint8_t       radius = 2;
    Tick               respawn_ticks = 0;
    /// When the next child may appear. Set on every death, so a wiped spawner
    /// refills one at a time instead of all at once.
    Tick               next_spawn_tick = 0;
    std::vector<NetId> children;
};

/// The actor this one is auto-attacking, Tibia style. Absent means "not
/// attacking", the same way CWalk's absence means "standing still". `target` is a
/// NetId, not an entity, so it survives the target dying and respawning.
struct CTarget {
    NetId target          = kInvalidNetId;
    Tick  next_swing_tick = 0;
};

/// An actor chasing another one: keep walking until it is within reach.
///
/// This exists because a one-shot route is not a chase. Clicking a monster used to
/// plan a path to the tile it was standing on; the monster walked away, the route
/// finished (or its last step was refused, since the target's tile is occupied),
/// and the attacker stood there with a target it could never reach — "it follows
/// halfway and stops attacking". sim::update_chasers replans whenever the target
/// moves, and stops as soon as the swing would land.
///
/// Held by monsters and players alike. Manual movement (World::cancel_path) drops
/// it, so a keypress or a click on the ground always takes control back.
struct CFollow {
    NetId   target = kInvalidNetId;
    /// Where the target was when the current route was planned; a chase replans
    /// when this stops matching, which is what makes it a chase.
    TilePos planned_for{};
    /// Floor of the plan, so a target taking a stair forces a replan rather than a
    /// walk toward a tile on another floor.
    Tick    next_replan_tick = 0;
};

/// Marks an actor that respawns instead of vanishing on death, and where. Players
/// get this (in solo and on the server); monsters do not, so killing a monster
/// despawns it outright.
struct CRespawn {
    TilePos point;
};

/// Present while an actor is dead and waiting to respawn. It cannot act and does
/// not occupy its tile. Only actors with CRespawn ever get one.
struct CDead {
    Tick respawn_tick = 0;
};

/// Phantom corpse: inventory on a tile that does NOT occupy occupancy_. No
/// CActor — invisible to snapshots until L2 wires a corpse view. Spawned when a
/// monster dies; the tile stays walkable.
struct CCorpse {};

/// An actor walking a precomputed route, one step per tile.
///
/// Present only while following a path; its absence means "not going anywhere",
/// the same way CWalk's absence means "standing still". The path is planned
/// ignoring other actors, so a step can be refused when someone is in the way —
/// `blocked_ticks` counts how long that has been true so a permanently blocked
/// follower gives up instead of shoving forever.
struct CPathFollow {
    std::vector<TilePos> path;
    std::size_t          next = 0;
    Tick                 blocked_ticks = 0;
};

}  // namespace sim

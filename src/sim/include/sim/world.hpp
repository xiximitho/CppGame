#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <entt/entity/registry.hpp>

#include "sim/components.hpp"
#include "sim/item_type.hpp"
#include "sim/monster_type.hpp"
#include "sim/pathfind.hpp"
#include "sim/rng.hpp"
#include "sim/tile_map.hpp"
#include "sim/types.hpp"

namespace sim {

/// Items lying on one tile. Kept with its TilePos so the client can render it
/// without reversing the packed occupancy key.
struct GroundPile {
    TilePos                tile;
    std::vector<ItemStack> items;
};

/// The authoritative game state and the only thing that advances time.
///
/// Contains no rendering, no sockets, no file access and no wall-clock reads.
/// The server owns one of these and drives it at a fixed rate; the tests drive
/// it directly; in solo play the client owns one too. That is the whole reason
/// the module boundary exists.
class World {
public:
    World() = default;
    /// `monsters` defaults to the built-in classes so every existing call site and
    /// test keeps working; the server and the solo session hand in the catalogue
    /// they loaded from assets/monsters.txt instead. `rng_seed` feeds loot rolls
    /// and anything else that must stay deterministic across platforms.
    explicit World(TileMap map, ItemTypeRegistry item_types = {},
                   MonsterRegistry monsters = default_monsters(),
                   std::uint64_t rng_seed = 1);

    Tick tick() const { return tick_; }

    /// Loot rolls and other sim-side randomness. Owned here so death does not
    /// need an Rng smuggled from the server loop.
    Rng&       rng() { return rng_; }
    const Rng& rng() const { return rng_; }

    TileMap&       map() { return map_; }
    const TileMap& map() const { return map_; }

    /// The item/type catalogue this world was built with. The source of truth
    /// for gameplay properties (blocking, pickable, ...). Empty by default, which
    /// is fine for tests that place tiles by hand.
    const ItemTypeRegistry& item_types() const { return item_types_; }

    /// The monster classes this world spawns from. Held here because spawners run
    /// inside the simulation and need it at runtime, not just at world build.
    const MonsterRegistry& monsters() const { return monsters_; }

    entt::registry&       registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

    NetId allocate_net_id();

    entt::entity spawn_actor(NetId net_id, TilePos at, std::uint16_t appearance,
                             Tick step_ticks = kDefaultStepTicks);
    void         despawn(NetId net_id);

    /// entt::null when the id is unknown.
    entt::entity lookup(NetId net_id) const;

    /// Which actor stands on (or is stepping into) this tile, if any.
    NetId occupant(TilePos pos) const;

    /// Whether `mover` may step from `from` to `to`. Rejects blocked tiles,
    /// occupied tiles, and diagonal moves that would clip the corner of a wall.
    bool can_enter(TilePos from, TilePos to, Direction dir, NetId mover) const;

    /// Starts a step. Returns false when the actor is already stepping or the
    /// destination is not enterable — but the actor still turns to face `dir`,
    /// which is what players expect when they walk into a wall.
    ///
    /// A primitive: it does NOT cancel an active path, because the path follower
    /// itself calls this every step. Callers acting on direct player input must
    /// call cancel_path() first — see the C2S_Input handler and SoloSession.
    bool request_walk(NetId net_id, Direction dir);

    bool request_turn(NetId net_id, Direction dir);

    /// Plans a route and starts following it. Returns false when there is no path,
    /// when `target` is not walkable, or when it is on another floor.
    ///
    /// Replaces any route already in progress. When called mid-step the route is
    /// planned from the tile being entered, not the one being left, so the actor
    /// does not turn around at the end of the current step.
    bool request_move_to(NetId net_id, TilePos target);

    /// Stops following a route AND stops chasing. This is what manual input calls:
    /// pressing a direction or clicking the ground takes control back from both
    /// auto-walking and an ongoing chase.
    ///
    /// The step already in flight still completes — an actor is never yanked
    /// backwards off a tile it is halfway onto.
    void cancel_path(NetId net_id);

    /// Drops the route but keeps any chase. sim::update_chasers uses this when the
    /// target comes into reach: the actor stops walking and keeps swinging.
    void stop_path(NetId net_id);

    /// Chases `target` until it is within attack reach; kInvalidNetId clears it.
    /// A primitive, like set_attack_target — sim::update_chasers does the walking.
    void request_follow(NetId net_id, NetId target);

    /// Sets the actor `attacker` auto-attacks; kInvalidNetId clears it. A
    /// primitive like request_walk — sim::update_combat drives the swings.
    void set_attack_target(NetId attacker, NetId target);

    /// Applies `amount` damage to an actor. Returns true if it died. A dead actor
    /// with CRespawn is marked dead and stops occupying its tile; one without is
    /// despawned. Never lets hp go negative.
    bool apply_damage(NetId net_id, std::int32_t amount);

    /// Brings a dead (CRespawn) actor back at its respawn point, full health.
    void respawn_actor(NetId net_id);

    /// Moves `item` from the actor's backpack into its equip slot, swapping any
    /// item already there back to the backpack. False if the actor does not have
    /// the item or it is not equippable. Untrusted input: validated here.
    bool equip(NetId net_id, ItemTypeId item);

    /// Moves whatever is in `slot` back to the backpack. False if the slot is
    /// empty.
    bool unequip(NetId net_id, EquipSlot slot);

    /// Drops an item stack onto a tile (merging with what is already there).
    void drop_item(TilePos tile, ItemStack stack);

    /// Phantom corpse at `tile` with `items` (may be empty). Does not occupy the
    /// tile. Returns the new entity.
    entt::entity spawn_corpse(TilePos tile, std::vector<ItemStack> items);

    /// Moves one stack from a phantom corpse into `taker`'s backpack. `index` is
    /// into the corpse inventory. Requires the taker to be on the same floor and
    /// within one tile (Chebyshev). Destroys the corpse when it becomes empty.
    /// False if out of reach, no backpack, bad index, or no corpse there.
    bool take_from_corpse(NetId taker, TilePos corpse_tile, std::size_t index);

    /// The item stacks lying on a tile, or nullptr if the tile is bare.
    const std::vector<ItemStack>* ground_items_at(TilePos tile) const;

    /// All ground piles, for the client to render. Keyed by packed tile.
    const std::unordered_map<std::uint64_t, GroundPile>& ground_piles() const {
        return ground_;
    }

    /// Attacks that landed since the last clear. update_combat appends here; the
    /// server sends them as S2C_Effect and the solo session feeds them to the
    /// client, then clears.
    const std::vector<AttackEvent>& attack_events() const { return attack_events_; }
    void push_attack_event(const AttackEvent& event) {
        attack_events_.push_back(event);
    }
    void clear_attack_events() { attack_events_.clear(); }

    /// Whether the actor is currently following a route.
    bool is_following_path(NetId net_id) const;

    /// Advances exactly one tick. Never called with a variable delta: a fixed
    /// step is what keeps server and client agreeing on what tick 4000 means.
    void step();

private:
    static std::uint64_t tile_key(TilePos pos);

    void occupy(TilePos pos, NetId net_id);
    void vacate(TilePos pos, NetId net_id);

    /// Moves the actor a floor when it just walked onto a stair tile. No-op when
    /// the tile is not a stair or the destination cannot take it.
    void apply_stairs(entt::entity entity);

    TileMap                  map_;
    ItemTypeRegistry         item_types_;
    MonsterRegistry          monsters_;
    entt::registry           registry_;
    std::vector<AttackEvent> attack_events_;
    Rng                      rng_{1};

    std::unordered_map<NetId, entt::entity>       by_net_id_;
    std::unordered_map<std::uint64_t, NetId>      occupancy_;
    std::unordered_map<std::uint64_t, GroundPile> ground_;

    /// Mutable scratch, not logical state: reused across calls so pathfinding does
    /// not allocate a map-sized working set per request.
    mutable Pathfinder pathfinder_;

    Tick  tick_         = 0;
    NetId next_net_id_  = 1;
};

}  // namespace sim

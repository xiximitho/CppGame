#pragma once

#include <cstdint>
#include <unordered_map>

#include <entt/entity/registry.hpp>

#include "sim/components.hpp"
#include "sim/item_type.hpp"
#include "sim/pathfind.hpp"
#include "sim/tile_map.hpp"
#include "sim/types.hpp"

namespace sim {

/// The authoritative game state and the only thing that advances time.
///
/// Contains no rendering, no sockets, no file access and no wall-clock reads.
/// The server owns one of these and drives it at a fixed rate; the tests drive
/// it directly; in solo play the client owns one too. That is the whole reason
/// the module boundary exists.
class World {
public:
    World() = default;
    explicit World(TileMap map, ItemTypeRegistry item_types = {});

    Tick tick() const { return tick_; }

    TileMap&       map() { return map_; }
    const TileMap& map() const { return map_; }

    /// The item/type catalogue this world was built with. The source of truth
    /// for gameplay properties (blocking, pickable, ...). Empty by default, which
    /// is fine for tests that place tiles by hand.
    const ItemTypeRegistry& item_types() const { return item_types_; }

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

    /// Stops following a route. The step already in flight still completes — an
    /// actor is never yanked backwards off a tile it is halfway onto.
    void cancel_path(NetId net_id);

    /// Sets the actor `attacker` auto-attacks; kInvalidNetId clears it. A
    /// primitive like request_walk — sim::update_combat drives the swings.
    void set_attack_target(NetId attacker, NetId target);

    /// Applies `amount` damage to an actor. Returns true if it died. A dead actor
    /// with CRespawn is marked dead and stops occupying its tile; one without is
    /// despawned. Never lets hp go negative.
    bool apply_damage(NetId net_id, std::int32_t amount);

    /// Brings a dead (CRespawn) actor back at its respawn point, full health.
    void respawn_actor(NetId net_id);

    /// Whether the actor is currently following a route.
    bool is_following_path(NetId net_id) const;

    /// Advances exactly one tick. Never called with a variable delta: a fixed
    /// step is what keeps server and client agreeing on what tick 4000 means.
    void step();

private:
    static std::uint64_t tile_key(TilePos pos);

    void occupy(TilePos pos, NetId net_id);
    void vacate(TilePos pos, NetId net_id);

    TileMap          map_;
    ItemTypeRegistry item_types_;
    entt::registry   registry_;

    std::unordered_map<NetId, entt::entity>  by_net_id_;
    std::unordered_map<std::uint64_t, NetId> occupancy_;

    /// Mutable scratch, not logical state: reused across calls so pathfinding does
    /// not allocate a map-sized working set per request.
    mutable Pathfinder pathfinder_;

    Tick  tick_         = 0;
    NetId next_net_id_  = 1;
};

}  // namespace sim

#pragma once

#include <cstdint>
#include <unordered_map>

#include <entt/entity/registry.hpp>

#include "sim/components.hpp"
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
    explicit World(TileMap map);

    Tick tick() const { return tick_; }

    TileMap&       map() { return map_; }
    const TileMap& map() const { return map_; }

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
    bool request_walk(NetId net_id, Direction dir);

    bool request_turn(NetId net_id, Direction dir);

    /// Advances exactly one tick. Never called with a variable delta: a fixed
    /// step is what keeps server and client agreeing on what tick 4000 means.
    void step();

private:
    static std::uint64_t tile_key(TilePos pos);

    void occupy(TilePos pos, NetId net_id);
    void vacate(TilePos pos, NetId net_id);

    TileMap        map_;
    entt::registry registry_;

    std::unordered_map<NetId, entt::entity>  by_net_id_;
    std::unordered_map<std::uint64_t, NetId> occupancy_;

    Tick  tick_         = 0;
    NetId next_net_id_  = 1;
};

}  // namespace sim

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sim/components.hpp"
#include "sim/snapshot.hpp"
#include "sim/tile_map.hpp"
#include "sim/types.hpp"

namespace client {

/// One tile's loot as the renderer sees it: where, and which icon to draw.
struct GroundItemView {
    sim::TilePos    tile;
    sim::ItemTypeId id = sim::kItemNone;
};

/// Everything the renderer is allowed to know about the world.
///
/// Note what is absent: no entt::registry, no World. The renderer draws a list of
/// ActorState and a TileMap, and cannot reach into simulation internals. That is
/// what lets the exact same rendering code serve both a local simulation and a
/// remote server that only ever sends this much.
struct WorldView {
    sim::TileMap                 map;
    std::vector<sim::ActorState> actors;
    sim::NetId                   local_id = sim::kInvalidNetId;
    sim::Tick                    tick = 0;
    /// False until the map dimensions and the local actor id are known.
    bool ready = false;

    /// The local player's gear and backpack, for the inventory panel. Filled by
    /// solo today; the remote session leaves them empty until inventory is sent
    /// over the wire (a known TODO).
    std::array<sim::ItemTypeId, sim::kEquipSlotCount> equipment{};
    std::vector<sim::ItemStack>                       inventory;

    /// Loot lying on the ground (one entry per tile, the top item), for drawing.
    /// Solo fills it; remote leaves it empty until it is sent over the wire.
    std::vector<GroundItemView> ground_items;
};

/// Where the world comes from. Solo runs the simulation in-process; Remote
/// receives it. The client cannot tell the difference downstream of here, which
/// is what keeps single-player from quietly growing rules the server does not
/// enforce.
class Session {
public:
    Session() = default;
    virtual ~Session() = default;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    /// Called once per frame: advances the local simulation or pumps the socket.
    virtual void update() = 0;

    /// Player intent: one step. Cancels any route in progress, because pressing a
    /// direction key while auto-walking must take back manual control.
    virtual void request_walk(sim::Direction dir) = 0;

    /// Player intent: walk to a tile. The route is planned and followed by the
    /// simulation — server-side in network play — so the client only names the
    /// destination and never a path.
    virtual void request_move_to(sim::TilePos target) = 0;

    /// Player intent: auto-attack `target` (kInvalidNetId clears it). The
    /// simulation owns the fight — server-side in network play — so the client
    /// only names who, never the damage.
    virtual void request_attack(sim::NetId target) = 0;

    /// Player intent: equip an item from the backpack, or clear an equip slot
    /// back to it. Server-validated, like every other intent.
    virtual void request_equip(sim::ItemTypeId item) = 0;
    virtual void request_unequip(sim::EquipSlot slot) = 0;

    virtual const WorldView& view() const = 0;

    /// Attack effects that occurred since the last call, for the client to
    /// render. Returns and clears them so each is drawn once.
    virtual std::vector<sim::AttackEvent> drain_effects() = 0;

    /// Short human-readable state for the window title / HUD.
    virtual std::string status_text() const = 0;

    /// False once the session is unrecoverable (disconnected, rejected).
    virtual bool alive() const = 0;
};

/// In-process simulation. `wanderers` extra actors are spawned so there is
/// something to watch besides the player.
///
/// `map_path` is an asset path (read through platform::vfs, so it also resolves
/// inside the APK on Android); the seeded procedural map is used when the file is
/// missing or malformed, which keeps a clone with no map file runnable.
std::unique_ptr<Session> make_solo_session(std::uint64_t seed, int wanderers,
                                           const std::string& map_path);

/// Connects to a server. Returns nullptr when the address cannot be resolved;
/// connection failures after that surface through alive().
std::unique_ptr<Session> make_remote_session(const std::string& host,
                                             std::uint16_t port,
                                             const std::string& player_name);

}  // namespace client

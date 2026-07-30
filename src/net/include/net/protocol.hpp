#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bitstream.hpp"
#include "sim/snapshot.hpp"
#include "sim/tile_map.hpp"
#include "sim/types.hpp"

namespace net {

/// Bumped on any wire-format change. Mismatched clients are rejected at Hello
/// rather than left to misparse a packet.
///
/// 2: added C2S_MoveTo (click-to-move).
/// 3: added C2S_Attack (auto-attack a target).
/// 4: added S2C_Effect (attack effect sprite).
/// 5: added C2S_Equip / C2S_Unequip (interactive inventory).
constexpr std::uint32_t kProtocolVersion = 6;

constexpr std::uint16_t kDefaultPort = 7777;

/// Comfortably below the ~1200 byte practical MTU for unreliable traffic and
/// large enough for a reliable map chunk, which ENet fragments for us.
constexpr std::size_t kMaxPacketBytes = 4096;

/// Map streaming granularity. Small chunks mean more messages but a smoother
/// trickle as the player walks; 16x16 keeps one chunk under a kilobyte.
constexpr int kChunkSize = 16;
constexpr int kChunkTileCount = kChunkSize * kChunkSize;

constexpr sim::TileId kMaxTileId = 1023;
constexpr int         kMaxCoord = 4095;
constexpr int         kMaxFloor = 15;

/// A snapshot carries at most this many actors. The area of interest can in
/// principle hold more; when it does, the server truncates rather than sending
/// an oversized packet. Real crowds need per-actor priority, which is the next
/// thing to build here.
constexpr std::size_t kMaxActorsPerSnapshot = 255;

constexpr std::size_t kMaxNameLength = 24;

enum class MsgId : std::uint8_t {
    Invalid = 0,

    // Client to server.
    C2S_Hello    = 1,
    C2S_Input    = 2,
    C2S_MoveTo   = 3,
    C2S_Attack   = 4,
    C2S_Equip    = 5,
    C2S_Unequip  = 6,

    // Server to client.
    S2C_Welcome  = 64,
    S2C_Reject   = 65,
    S2C_Snapshot = 66,
    S2C_MapChunk = 67,
    S2C_Effect   = 68,
};

struct HelloMsg {
    std::uint32_t protocol = kProtocolVersion;
    std::string   name;
    /// sim::content_hash of the client's item catalogue.
    ///
    /// The server reads content from its SQLite database and the client from the
    /// baked blob, so they can legitimately disagree — someone edits an item and
    /// forgets to re-bake, and suddenly the two sides disagree about what blocks and
    /// how hard a sword hits, with nothing reporting it. This is checked exactly like
    /// the protocol version, and for the same reason: a mismatch caught at the
    /// handshake is a message, a mismatch missed is a bug hunt.
    std::uint64_t content_hash = 0;
};

/// One player intent for one tick. There is exactly one unit per player, so this
/// stays tiny; `walk` false with a direction means "turn only".
struct InputMsg {
    sim::Tick      client_tick = 0;
    bool           walk = false;
    sim::Direction dir = sim::Direction::South;
};

/// Click-to-move: the player names a destination and the server does the walking.
///
/// The route is planned server-side on purpose. The client only holds the map
/// chunks it has been streamed, so it cannot plan across parts of the world it has
/// never seen — and an authoritative server must not take the client's word for a
/// route anyway.
struct MoveToMsg {
    sim::TilePos target;
};

/// Auto-attack intent: the player names a target actor and the server swings at
/// it each cooldown while in range. `target` == kInvalidNetId clears the target.
/// Like MoveToMsg, only the intent crosses the wire — the server owns the fight.
struct AttackMsg {
    sim::NetId target = sim::kInvalidNetId;
};

/// Interactive inventory intents. Equip an item id from the backpack, or clear
/// an equip slot back to it. Server-validated, like every other intent.
struct EquipMsg {
    sim::ItemTypeId item = sim::kItemNone;
};
struct UnequipMsg {
    std::uint8_t slot = 0;  ///< index into EquipSlot; server bounds-checks
};

struct WelcomeMsg {
    sim::NetId    your_id = sim::kInvalidNetId;
    sim::Tick     tick = 0;
    std::uint16_t map_width = 0;
    std::uint16_t map_height = 0;
    std::uint8_t  map_floors = 0;
    sim::TilePos  spawn;
};

struct RejectMsg {
    std::string reason;
};

/// A square of map, sent reliably as the player approaches it. The client holds
/// only the chunks it has been told about, so the world may be far larger than
/// anything a client ever sees.
/// A one-shot attack effect for the client to render: a burst at `to` for melee
/// (from == to), or a projectile travelling from -> to for ranged. Pure
/// presentation — unreliable, and a lost one just means a missed spark.
struct EffectMsg {
    sim::TilePos from;
    sim::TilePos to;
    std::uint8_t effect = 0;
};

struct MapChunkMsg {
    std::int16_t            chunk_x = 0;  ///< in tiles, top-left corner
    std::int16_t            chunk_y = 0;
    std::int8_t             z = 0;
    std::vector<sim::Tile>  tiles;        ///< exactly kChunkTileCount, row major
};

// --- writing ---------------------------------------------------------------
void write_hello(core::BitWriter& writer, const HelloMsg& msg);
void write_input(core::BitWriter& writer, const InputMsg& msg);
void write_move_to(core::BitWriter& writer, const MoveToMsg& msg);
void write_attack(core::BitWriter& writer, const AttackMsg& msg);
void write_equip(core::BitWriter& writer, const EquipMsg& msg);
void write_unequip(core::BitWriter& writer, const UnequipMsg& msg);
void write_welcome(core::BitWriter& writer, const WelcomeMsg& msg);
void write_reject(core::BitWriter& writer, const RejectMsg& msg);
void write_snapshot(core::BitWriter& writer, const sim::Snapshot& snapshot);
void write_map_chunk(core::BitWriter& writer, const MapChunkMsg& msg);
void write_effect(core::BitWriter& writer, const EffectMsg& msg);

// --- reading ---------------------------------------------------------------
/// Returns MsgId::Invalid on an empty or truncated packet.
MsgId read_msg_id(core::BitReader& reader);

/// Each returns false when the packet was truncated or malformed. On false the
/// output struct is left in an unspecified state and must not be used.
bool read_hello(core::BitReader& reader, HelloMsg& out);
bool read_input(core::BitReader& reader, InputMsg& out);
bool read_move_to(core::BitReader& reader, MoveToMsg& out);
bool read_attack(core::BitReader& reader, AttackMsg& out);
bool read_equip(core::BitReader& reader, EquipMsg& out);
bool read_unequip(core::BitReader& reader, UnequipMsg& out);
bool read_welcome(core::BitReader& reader, WelcomeMsg& out);
bool read_reject(core::BitReader& reader, RejectMsg& out);
bool read_snapshot(core::BitReader& reader, sim::Snapshot& out);
bool read_map_chunk(core::BitReader& reader, MapChunkMsg& out);
bool read_effect(core::BitReader& reader, EffectMsg& out);

}  // namespace net

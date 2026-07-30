#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/bitstream.hpp"
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
constexpr std::uint32_t kProtocolVersion = 4;

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
    C2S_Hello  = 1,
    C2S_Input  = 2,
    C2S_MoveTo = 3,
    C2S_Attack = 4,

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
void write_hello(BitWriter& writer, const HelloMsg& msg);
void write_input(BitWriter& writer, const InputMsg& msg);
void write_move_to(BitWriter& writer, const MoveToMsg& msg);
void write_attack(BitWriter& writer, const AttackMsg& msg);
void write_welcome(BitWriter& writer, const WelcomeMsg& msg);
void write_reject(BitWriter& writer, const RejectMsg& msg);
void write_snapshot(BitWriter& writer, const sim::Snapshot& snapshot);
void write_map_chunk(BitWriter& writer, const MapChunkMsg& msg);
void write_effect(BitWriter& writer, const EffectMsg& msg);

// --- reading ---------------------------------------------------------------
/// Returns MsgId::Invalid on an empty or truncated packet.
MsgId read_msg_id(BitReader& reader);

/// Each returns false when the packet was truncated or malformed. On false the
/// output struct is left in an unspecified state and must not be used.
bool read_hello(BitReader& reader, HelloMsg& out);
bool read_input(BitReader& reader, InputMsg& out);
bool read_move_to(BitReader& reader, MoveToMsg& out);
bool read_attack(BitReader& reader, AttackMsg& out);
bool read_welcome(BitReader& reader, WelcomeMsg& out);
bool read_reject(BitReader& reader, RejectMsg& out);
bool read_snapshot(BitReader& reader, sim::Snapshot& out);
bool read_map_chunk(BitReader& reader, MapChunkMsg& out);
bool read_effect(BitReader& reader, EffectMsg& out);

}  // namespace net

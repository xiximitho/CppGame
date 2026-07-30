#include "net/protocol.hpp"

#include <algorithm>

namespace net {
namespace {

constexpr int kDirectionBits = 3;
constexpr int kTileIdBits = 10;  // matches kMaxTileId

void write_msg_id(core::BitWriter& writer, MsgId id) {
    writer.write_bits(static_cast<std::uint32_t>(id), 8);
}

void write_tile_pos(core::BitWriter& writer, sim::TilePos pos) {
    writer.write_ranged(pos.x, 0, kMaxCoord);
    writer.write_ranged(pos.y, 0, kMaxCoord);
    writer.write_ranged(pos.z, 0, kMaxFloor);
}

sim::TilePos read_tile_pos(core::BitReader& reader) {
    sim::TilePos pos;
    pos.x = static_cast<std::int16_t>(reader.read_ranged(0, kMaxCoord));
    pos.y = static_cast<std::int16_t>(reader.read_ranged(0, kMaxCoord));
    pos.z = static_cast<std::int8_t>(reader.read_ranged(0, kMaxFloor));
    return pos;
}

void write_direction(core::BitWriter& writer, sim::Direction dir) {
    writer.write_bits(static_cast<std::uint32_t>(dir), kDirectionBits);
}

sim::Direction read_direction(core::BitReader& reader) {
    const std::uint32_t raw = reader.read_bits(kDirectionBits);
    // 3 bits cannot exceed the 8 valid directions, so no clamp is needed — but
    // the cast is stated explicitly so widening the enum does not silently
    // produce an invalid value.
    return static_cast<sim::Direction>(raw & 0x7U);
}

}  // namespace

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

void write_hello(core::BitWriter& writer, const HelloMsg& msg) {
    write_msg_id(writer, MsgId::C2S_Hello);
    writer.write_bits(msg.protocol, 16);
    writer.write_string(msg.name, kMaxNameLength);
    // BitWriter deals in 32-bit values, so the digest goes out as two halves.
    writer.write_bits(static_cast<std::uint32_t>(msg.content_hash >> 32U), 32);
    writer.write_bits(static_cast<std::uint32_t>(msg.content_hash & 0xFFFFFFFFU),
                      32);
    writer.flush();
}

void write_input(core::BitWriter& writer, const InputMsg& msg) {
    write_msg_id(writer, MsgId::C2S_Input);
    writer.write_bits(msg.client_tick, 32);
    writer.write_bool(msg.walk);
    write_direction(writer, msg.dir);
    writer.flush();
}

void write_move_to(core::BitWriter& writer, const MoveToMsg& msg) {
    write_msg_id(writer, MsgId::C2S_MoveTo);
    write_tile_pos(writer, msg.target);
    writer.flush();
}

void write_attack(core::BitWriter& writer, const AttackMsg& msg) {
    write_msg_id(writer, MsgId::C2S_Attack);
    writer.write_bits(msg.target, 32);
    writer.flush();
}

void write_equip(core::BitWriter& writer, const EquipMsg& msg) {
    write_msg_id(writer, MsgId::C2S_Equip);
    writer.write_bits(msg.item, 16);
    writer.flush();
}

void write_unequip(core::BitWriter& writer, const UnequipMsg& msg) {
    write_msg_id(writer, MsgId::C2S_Unequip);
    writer.write_bits(msg.slot, 8);
    writer.flush();
}

void write_effect(core::BitWriter& writer, const EffectMsg& msg) {
    write_msg_id(writer, MsgId::S2C_Effect);
    write_tile_pos(writer, msg.from);
    write_tile_pos(writer, msg.to);
    writer.write_bits(msg.effect, 8);
    writer.flush();
}

void write_welcome(core::BitWriter& writer, const WelcomeMsg& msg) {
    write_msg_id(writer, MsgId::S2C_Welcome);
    writer.write_bits(msg.your_id, 32);
    writer.write_bits(msg.tick, 32);
    writer.write_ranged(msg.map_width, 1, kMaxCoord + 1);
    writer.write_ranged(msg.map_height, 1, kMaxCoord + 1);
    writer.write_ranged(msg.map_floors, 1, kMaxFloor + 1);
    write_tile_pos(writer, msg.spawn);
    writer.flush();
}

void write_reject(core::BitWriter& writer, const RejectMsg& msg) {
    write_msg_id(writer, MsgId::S2C_Reject);
    writer.write_string(msg.reason, 63);
    writer.flush();
}

void write_snapshot(core::BitWriter& writer, const sim::Snapshot& snapshot) {
    write_msg_id(writer, MsgId::S2C_Snapshot);
    writer.write_bits(snapshot.tick, 32);

    const std::size_t count =
        std::min(snapshot.actors.size(), kMaxActorsPerSnapshot);
    writer.write_bits(static_cast<std::uint32_t>(count), 8);

    for (std::size_t i = 0; i < count; ++i) {
        const sim::ActorState& actor = snapshot.actors[i];
        writer.write_bits(actor.net_id, 32);
        write_tile_pos(writer, actor.tile);
        write_direction(writer, actor.facing);
        writer.write_bool(actor.walking);
        // Walk fields only exist when walking, so a standing actor is 4 bits
        // cheaper. Most actors in a crowd are standing.
        if (actor.walking) {
            write_direction(writer, actor.walk_dir);
            writer.write_bits(actor.walk_progress, 8);
        }
        writer.write_bits(actor.appearance, 16);
        writer.write_ranged(actor.hp, 0, 32767);
        writer.write_ranged(actor.max_hp, 0, 32767);
    }
    writer.flush();
}

void write_map_chunk(core::BitWriter& writer, const MapChunkMsg& msg) {
    write_msg_id(writer, MsgId::S2C_MapChunk);
    writer.write_ranged(msg.chunk_x, 0, kMaxCoord);
    writer.write_ranged(msg.chunk_y, 0, kMaxCoord);
    writer.write_ranged(msg.z, 0, kMaxFloor);

    for (int i = 0; i < kChunkTileCount; ++i) {
        // A short vector is padded with empty tiles rather than truncating the
        // message, keeping the format fixed-size and trivially parseable.
        const sim::Tile tile = (static_cast<std::size_t>(i) < msg.tiles.size())
                                   ? msg.tiles[static_cast<std::size_t>(i)]
                                   : sim::Tile{};
        writer.write_bits(std::min(tile.ground, kMaxTileId), kTileIdBits);
        writer.write_bits(std::min(tile.object, kMaxTileId), kTileIdBits);
        writer.write_bool(tile.blocking);
    }
    writer.flush();
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

MsgId read_msg_id(core::BitReader& reader) {
    const std::uint32_t raw = reader.read_bits(8);
    if (reader.overflowed()) {
        return MsgId::Invalid;
    }
    switch (static_cast<MsgId>(raw)) {
        case MsgId::C2S_Hello:    return MsgId::C2S_Hello;
        case MsgId::C2S_Input:    return MsgId::C2S_Input;
        case MsgId::C2S_MoveTo:   return MsgId::C2S_MoveTo;
        case MsgId::C2S_Attack:   return MsgId::C2S_Attack;
        case MsgId::C2S_Equip:    return MsgId::C2S_Equip;
        case MsgId::C2S_Unequip:  return MsgId::C2S_Unequip;
        case MsgId::S2C_Welcome:  return MsgId::S2C_Welcome;
        case MsgId::S2C_Reject:   return MsgId::S2C_Reject;
        case MsgId::S2C_Snapshot: return MsgId::S2C_Snapshot;
        case MsgId::S2C_MapChunk: return MsgId::S2C_MapChunk;
        case MsgId::S2C_Effect:   return MsgId::S2C_Effect;
        case MsgId::Invalid:      break;
    }
    return MsgId::Invalid;
}

bool read_hello(core::BitReader& reader, HelloMsg& out) {
    out.protocol = reader.read_bits(16);
    out.name = reader.read_string(kMaxNameLength);
    const std::uint64_t high = reader.read_bits(32);
    const std::uint64_t low = reader.read_bits(32);
    out.content_hash = (high << 32U) | low;
    return !reader.overflowed();
}

bool read_input(core::BitReader& reader, InputMsg& out) {
    out.client_tick = reader.read_bits(32);
    out.walk = reader.read_bool();
    out.dir = read_direction(reader);
    return !reader.overflowed();
}

bool read_move_to(core::BitReader& reader, MoveToMsg& out) {
    out.target = read_tile_pos(reader);
    return !reader.overflowed();
}

bool read_attack(core::BitReader& reader, AttackMsg& out) {
    out.target = reader.read_bits(32);
    return !reader.overflowed();
}

bool read_equip(core::BitReader& reader, EquipMsg& out) {
    out.item = static_cast<sim::ItemTypeId>(reader.read_bits(16));
    return !reader.overflowed();
}

bool read_unequip(core::BitReader& reader, UnequipMsg& out) {
    out.slot = static_cast<std::uint8_t>(reader.read_bits(8));
    return !reader.overflowed();
}

bool read_effect(core::BitReader& reader, EffectMsg& out) {
    out.from = read_tile_pos(reader);
    out.to = read_tile_pos(reader);
    out.effect = static_cast<std::uint8_t>(reader.read_bits(8));
    return !reader.overflowed();
}

bool read_welcome(core::BitReader& reader, WelcomeMsg& out) {
    out.your_id = reader.read_bits(32);
    out.tick = reader.read_bits(32);
    out.map_width =
        static_cast<std::uint16_t>(reader.read_ranged(1, kMaxCoord + 1));
    out.map_height =
        static_cast<std::uint16_t>(reader.read_ranged(1, kMaxCoord + 1));
    out.map_floors =
        static_cast<std::uint8_t>(reader.read_ranged(1, kMaxFloor + 1));
    out.spawn = read_tile_pos(reader);
    return !reader.overflowed();
}

bool read_reject(core::BitReader& reader, RejectMsg& out) {
    out.reason = reader.read_string(63);
    return !reader.overflowed();
}

bool read_snapshot(core::BitReader& reader, sim::Snapshot& out) {
    out.tick = reader.read_bits(32);
    const std::uint32_t count = reader.read_bits(8);

    out.actors.clear();
    out.actors.reserve(count);

    for (std::uint32_t i = 0; i < count; ++i) {
        sim::ActorState actor;
        actor.net_id = reader.read_bits(32);
        actor.tile = read_tile_pos(reader);
        actor.facing = read_direction(reader);
        actor.walking = reader.read_bool();
        if (actor.walking) {
            actor.walk_dir = read_direction(reader);
            actor.walk_progress = static_cast<std::uint8_t>(reader.read_bits(8));
        }
        actor.appearance = static_cast<std::uint16_t>(reader.read_bits(16));
        actor.hp = static_cast<std::int16_t>(reader.read_ranged(0, 32767));
        actor.max_hp = static_cast<std::int16_t>(reader.read_ranged(0, 32767));

        // Bail out the moment the packet runs short instead of appending
        // garbage actors built from zero bits.
        if (reader.overflowed()) {
            return false;
        }
        out.actors.push_back(actor);
    }

    return !reader.overflowed();
}

bool read_map_chunk(core::BitReader& reader, MapChunkMsg& out) {
    out.chunk_x = static_cast<std::int16_t>(reader.read_ranged(0, kMaxCoord));
    out.chunk_y = static_cast<std::int16_t>(reader.read_ranged(0, kMaxCoord));
    out.z = static_cast<std::int8_t>(reader.read_ranged(0, kMaxFloor));

    out.tiles.clear();
    out.tiles.resize(kChunkTileCount);
    for (int i = 0; i < kChunkTileCount; ++i) {
        sim::Tile& tile = out.tiles[static_cast<std::size_t>(i)];
        tile.ground = static_cast<sim::TileId>(reader.read_bits(kTileIdBits));
        tile.object = static_cast<sim::TileId>(reader.read_bits(kTileIdBits));
        tile.blocking = reader.read_bool();
    }

    return !reader.overflowed();
}

}  // namespace net

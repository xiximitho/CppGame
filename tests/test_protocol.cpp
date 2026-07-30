#include <doctest/doctest.h>

#include <array>
#include <vector>

#include "net/protocol.hpp"
#include "sim/tile_ids.hpp"

namespace {

/// Writes with `write_fn`, then hands back a reader positioned just after the
/// message id, which is what the receive path does.
template <typename WriteFn>
std::vector<std::uint8_t> encode(WriteFn write_fn) {
    std::array<std::uint8_t, net::kMaxPacketBytes> buffer{};
    core::BitWriter writer(buffer.data(), buffer.size());
    write_fn(writer);
    REQUIRE_FALSE(writer.overflowed());
    return std::vector<std::uint8_t>(buffer.begin(),
                                     buffer.begin() +
                                         static_cast<std::ptrdiff_t>(
                                             writer.bytes_written()));
}

}  // namespace

TEST_CASE("hello round trips and carries the protocol version") {
    // A digest with both halves populated and neither symmetric, so a swapped or
    // dropped 32-bit half shows up rather than surviving by luck.
    constexpr std::uint64_t kHash = 0xDEADBEEF12345678ULL;

    const auto packet = encode([](core::BitWriter& writer) {
        net::HelloMsg hello;
        hello.protocol = net::kProtocolVersion;
        hello.name = "felipe";
        hello.content_hash = kHash;
        net::write_hello(writer, hello);
    });

    core::BitReader reader(packet.data(), packet.size());
    REQUIRE(net::read_msg_id(reader) == net::MsgId::C2S_Hello);

    net::HelloMsg decoded;
    REQUIRE(net::read_hello(reader, decoded));
    CHECK(decoded.protocol == net::kProtocolVersion);
    CHECK(decoded.name == "felipe");
    CHECK(decoded.content_hash == kHash);
}

TEST_CASE("input round trips") {
    const auto packet = encode([](core::BitWriter& writer) {
        net::InputMsg input;
        input.client_tick = 123456;
        input.walk = true;
        input.dir = sim::Direction::NorthWest;
        net::write_input(writer, input);
    });

    core::BitReader reader(packet.data(), packet.size());
    REQUIRE(net::read_msg_id(reader) == net::MsgId::C2S_Input);

    net::InputMsg decoded;
    REQUIRE(net::read_input(reader, decoded));
    CHECK(decoded.client_tick == 123456);
    CHECK(decoded.walk);
    CHECK(decoded.dir == sim::Direction::NorthWest);
}

TEST_CASE("an input packet is tiny") {
    // 8 bits id + 32 bits tick + 1 bit walk + 3 bits direction = 44 bits.
    // If this ever grows unexpectedly, a field was added without thinking about
    // the per-tick cost.
    const auto packet = encode([](core::BitWriter& writer) {
        net::write_input(writer, net::InputMsg{1, true, sim::Direction::East});
    });
    CHECK(packet.size() == 6);
}

TEST_CASE("move-to round trips") {
    const auto packet = encode([](core::BitWriter& writer) {
        net::write_move_to(writer, net::MoveToMsg{sim::TilePos{123, 456, 2}});
    });

    core::BitReader reader(packet.data(), packet.size());
    REQUIRE(net::read_msg_id(reader) == net::MsgId::C2S_MoveTo);

    net::MoveToMsg decoded;
    REQUIRE(net::read_move_to(reader, decoded));
    CHECK(decoded.target == sim::TilePos{123, 456, 2});
}

TEST_CASE("a move-to packet is small") {
    // 8 bits id + 12 + 12 + 4 for the tile = 36 bits. Click-to-move must not cost
    // more on the wire than the thing it replaces.
    const auto packet = encode([](core::BitWriter& writer) {
        net::write_move_to(writer, net::MoveToMsg{sim::TilePos{10, 10, 0}});
    });
    CHECK(packet.size() == 5);
}

TEST_CASE("the protocol version is bumped past the pre-move-to format") {
    // C2S_MoveTo was added after version 1. A client that predates it must be
    // rejected at Hello rather than left to misparse.
    CHECK(net::kProtocolVersion >= 2);
}

TEST_CASE("attack round trips") {
    const auto packet = encode([](core::BitWriter& writer) {
        net::write_attack(writer, net::AttackMsg{4242});
    });

    core::BitReader reader(packet.data(), packet.size());
    REQUIRE(net::read_msg_id(reader) == net::MsgId::C2S_Attack);

    net::AttackMsg decoded;
    REQUIRE(net::read_attack(reader, decoded));
    CHECK(decoded.target == 4242U);
}

TEST_CASE("equip and unequip round trip") {
    const auto equip_packet = encode([](core::BitWriter& writer) {
        net::write_equip(writer, net::EquipMsg{sim::tiles::kBow});
    });
    core::BitReader er(equip_packet.data(), equip_packet.size());
    REQUIRE(net::read_msg_id(er) == net::MsgId::C2S_Equip);
    net::EquipMsg equip;
    REQUIRE(net::read_equip(er, equip));
    CHECK(equip.item == sim::tiles::kBow);

    const auto unequip_packet = encode([](core::BitWriter& writer) {
        net::write_unequip(writer, net::UnequipMsg{3});
    });
    core::BitReader ur(unequip_packet.data(), unequip_packet.size());
    REQUIRE(net::read_msg_id(ur) == net::MsgId::C2S_Unequip);
    net::UnequipMsg unequip;
    REQUIRE(net::read_unequip(ur, unequip));
    CHECK(unequip.slot == 3U);
}

TEST_CASE("welcome round trips") {
    const auto packet = encode([](core::BitWriter& writer) {
        net::WelcomeMsg welcome;
        welcome.your_id = 42;
        welcome.tick = 9001;
        welcome.map_width = 96;
        welcome.map_height = 96;
        welcome.map_floors = 3;
        welcome.spawn = sim::TilePos{33, 44, 1};
        net::write_welcome(writer, welcome);
    });

    core::BitReader reader(packet.data(), packet.size());
    REQUIRE(net::read_msg_id(reader) == net::MsgId::S2C_Welcome);

    net::WelcomeMsg decoded;
    REQUIRE(net::read_welcome(reader, decoded));
    CHECK(decoded.your_id == 42);
    CHECK(decoded.tick == 9001);
    CHECK(decoded.map_width == 96);
    CHECK(decoded.map_height == 96);
    CHECK(decoded.map_floors == 3);
    CHECK(decoded.spawn == sim::TilePos{33, 44, 1});
}

TEST_CASE("a snapshot round trips walking and standing actors") {
    sim::Snapshot original;
    original.tick = 777;

    sim::ActorState standing;
    standing.net_id = 1;
    standing.tile = sim::TilePos{10, 20, 0};
    standing.facing = sim::Direction::West;
    standing.walking = false;
    standing.appearance = 5;
    standing.hp = 80;
    standing.max_hp = 100;
    original.actors.push_back(standing);

    sim::ActorState walking;
    walking.net_id = 999999;
    walking.tile = sim::TilePos{11, 21, 2};
    walking.facing = sim::Direction::SouthEast;
    walking.walking = true;
    walking.walk_dir = sim::Direction::SouthEast;
    walking.walk_progress = 200;
    walking.appearance = 7;
    walking.hp = 12;
    walking.max_hp = 12;
    original.actors.push_back(walking);

    const auto packet = encode([&original](core::BitWriter& writer) {
        net::write_snapshot(writer, original);
    });

    core::BitReader reader(packet.data(), packet.size());
    REQUIRE(net::read_msg_id(reader) == net::MsgId::S2C_Snapshot);

    sim::Snapshot decoded;
    REQUIRE(net::read_snapshot(reader, decoded));
    REQUIRE(decoded.actors.size() == 2);
    CHECK(decoded.tick == 777);

    CHECK(decoded.actors[0].net_id == 1);
    CHECK(decoded.actors[0].tile == sim::TilePos{10, 20, 0});
    CHECK(decoded.actors[0].facing == sim::Direction::West);
    CHECK_FALSE(decoded.actors[0].walking);
    CHECK(decoded.actors[0].hp == 80);

    CHECK(decoded.actors[1].net_id == 999999);
    CHECK(decoded.actors[1].tile == sim::TilePos{11, 21, 2});
    CHECK(decoded.actors[1].walking);
    CHECK(decoded.actors[1].walk_dir == sim::Direction::SouthEast);
    CHECK(decoded.actors[1].walk_progress == 200);
    CHECK(decoded.actors[1].appearance == 7);
}

TEST_CASE("a standing actor costs fewer bits than a walking one") {
    // Guards the optimisation in write_snapshot: walk fields are omitted when the
    // actor is not moving, which matters because most of a crowd stands still.
    sim::ActorState actor;
    actor.net_id = 1;
    actor.tile = sim::TilePos{10, 10, 0};

    sim::Snapshot standing;
    standing.actors.push_back(actor);

    actor.walking = true;
    actor.walk_progress = 128;
    sim::Snapshot walking;
    walking.actors.push_back(actor);

    const auto standing_packet = encode(
        [&standing](core::BitWriter& writer) { net::write_snapshot(writer, standing); });
    const auto walking_packet = encode(
        [&walking](core::BitWriter& writer) { net::write_snapshot(writer, walking); });

    CHECK(standing_packet.size() < walking_packet.size());
}

TEST_CASE("an empty snapshot is valid") {
    sim::Snapshot empty;
    empty.tick = 5;

    const auto packet =
        encode([&empty](core::BitWriter& writer) { net::write_snapshot(writer, empty); });

    core::BitReader reader(packet.data(), packet.size());
    REQUIRE(net::read_msg_id(reader) == net::MsgId::S2C_Snapshot);

    sim::Snapshot decoded;
    REQUIRE(net::read_snapshot(reader, decoded));
    CHECK(decoded.tick == 5);
    CHECK(decoded.actors.empty());
}

TEST_CASE("a truncated snapshot is rejected instead of yielding phantom actors") {
    // The defence that matters: a short packet must not produce actors built out
    // of zero bits, which would teleport things to tile (0,0).
    sim::Snapshot original;
    original.tick = 1;
    for (int i = 0; i < 10; ++i) {
        sim::ActorState actor;
        actor.net_id = static_cast<sim::NetId>(i + 1);
        actor.tile = sim::TilePos{static_cast<std::int16_t>(i), 5, 0};
        original.actors.push_back(actor);
    }

    const auto packet = encode(
        [&original](core::BitWriter& writer) { net::write_snapshot(writer, original); });

    // Cut the packet in half.
    core::BitReader reader(packet.data(), packet.size() / 2);
    REQUIRE(net::read_msg_id(reader) == net::MsgId::S2C_Snapshot);

    sim::Snapshot decoded;
    CHECK_FALSE(net::read_snapshot(reader, decoded));
}

TEST_CASE("an empty packet yields an invalid message id rather than misparsing") {
    core::BitReader reader(nullptr, 0);
    CHECK(net::read_msg_id(reader) == net::MsgId::Invalid);
}

TEST_CASE("an unknown message id is reported as invalid") {
    std::array<std::uint8_t, 4> buffer{};
    buffer[0] = 200;  // not a defined MsgId

    core::BitReader reader(buffer.data(), buffer.size());
    CHECK(net::read_msg_id(reader) == net::MsgId::Invalid);
}

TEST_CASE("a map chunk round trips every tile") {
    net::MapChunkMsg original;
    original.chunk_x = 32;
    original.chunk_y = 48;
    original.z = 1;
    original.tiles.resize(net::kChunkTileCount);
    for (int i = 0; i < net::kChunkTileCount; ++i) {
        sim::Tile& tile = original.tiles[static_cast<std::size_t>(i)];
        tile.ground = static_cast<sim::TileId>(i % 4 == 0 ? sim::tiles::kWater
                                                          : sim::tiles::kGrass);
        tile.object = (i % 7 == 0) ? sim::tiles::kWall : sim::kTileEmpty;
        tile.blocking = (i % 7 == 0);
    }

    const auto packet = encode([&original](core::BitWriter& writer) {
        net::write_map_chunk(writer, original);
    });

    core::BitReader reader(packet.data(), packet.size());
    REQUIRE(net::read_msg_id(reader) == net::MsgId::S2C_MapChunk);

    net::MapChunkMsg decoded;
    REQUIRE(net::read_map_chunk(reader, decoded));
    CHECK(decoded.chunk_x == 32);
    CHECK(decoded.chunk_y == 48);
    CHECK(decoded.z == 1);
    REQUIRE(decoded.tiles.size() == static_cast<std::size_t>(net::kChunkTileCount));

    for (int i = 0; i < net::kChunkTileCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        CHECK(decoded.tiles[index].ground == original.tiles[index].ground);
        CHECK(decoded.tiles[index].object == original.tiles[index].object);
        CHECK(decoded.tiles[index].blocking == original.tiles[index].blocking);
    }
}

TEST_CASE("a map chunk fits in one packet") {
    // 256 tiles at 21 bits each is about 672 bytes. If a tile grows a field, this
    // is the test that says whether chunks still fit or need to shrink.
    net::MapChunkMsg chunk;
    chunk.tiles.resize(net::kChunkTileCount);

    const auto packet =
        encode([&chunk](core::BitWriter& writer) { net::write_map_chunk(writer, chunk); });
    CHECK(packet.size() < net::kMaxPacketBytes);
}

TEST_CASE("a short tile vector is padded rather than truncating the message") {
    net::MapChunkMsg original;
    original.tiles.resize(10);
    original.tiles[0].ground = sim::tiles::kStone;

    const auto packet = encode([&original](core::BitWriter& writer) {
        net::write_map_chunk(writer, original);
    });

    core::BitReader reader(packet.data(), packet.size());
    REQUIRE(net::read_msg_id(reader) == net::MsgId::S2C_MapChunk);

    net::MapChunkMsg decoded;
    REQUIRE(net::read_map_chunk(reader, decoded));
    REQUIRE(decoded.tiles.size() == static_cast<std::size_t>(net::kChunkTileCount));
    CHECK(decoded.tiles[0].ground == sim::tiles::kStone);
    CHECK(decoded.tiles[11].ground == sim::kTileEmpty);
}

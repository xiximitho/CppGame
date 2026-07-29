#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "net/bitstream.hpp"

using net::bits_required;
using net::BitReader;
using net::BitWriter;

TEST_CASE("bits_required covers the inclusive range") {
    CHECK(bits_required(0) == 1);
    CHECK(bits_required(1) == 1);
    CHECK(bits_required(2) == 2);
    CHECK(bits_required(3) == 2);
    CHECK(bits_required(4) == 3);
    CHECK(bits_required(255) == 8);
    CHECK(bits_required(256) == 9);
    CHECK(bits_required(0xFFFFFFFFU) == 32);
}

TEST_CASE("fields survive a write/read round trip at odd bit widths") {
    std::array<std::uint8_t, 64> buffer{};
    BitWriter writer(buffer.data(), buffer.size());

    writer.write_bits(1, 1);
    writer.write_bits(5, 3);
    writer.write_bits(1000, 10);
    writer.write_bool(false);
    writer.write_bits(0xDEADBEEFU, 32);
    writer.write_bits(7, 3);
    writer.flush();

    REQUIRE_FALSE(writer.overflowed());

    BitReader reader(buffer.data(), writer.bytes_written());
    CHECK(reader.read_bits(1) == 1U);
    CHECK(reader.read_bits(3) == 5U);
    CHECK(reader.read_bits(10) == 1000U);
    CHECK(reader.read_bool() == false);
    CHECK(reader.read_bits(32) == 0xDEADBEEFU);
    CHECK(reader.read_bits(3) == 7U);
    CHECK_FALSE(reader.overflowed());
}

TEST_CASE("ranged values round trip, including negative ranges") {
    std::array<std::uint8_t, 32> buffer{};
    BitWriter writer(buffer.data(), buffer.size());

    writer.write_ranged(-100, -1000, 1000);
    writer.write_ranged(0, -1, 1);
    writer.write_ranged(4095, 0, 4095);
    writer.write_ranged(-32768, -32768, 32767);
    writer.flush();

    REQUIRE_FALSE(writer.overflowed());

    BitReader reader(buffer.data(), writer.bytes_written());
    CHECK(reader.read_ranged(-1000, 1000) == -100);
    CHECK(reader.read_ranged(-1, 1) == 0);
    CHECK(reader.read_ranged(0, 4095) == 4095);
    CHECK(reader.read_ranged(-32768, 32767) == -32768);
}

TEST_CASE("ranged writes use only as many bits as the range needs") {
    // The whole reason this class exists. A direction has 8 states and must not
    // cost more than 3 bits.
    std::array<std::uint8_t, 32> buffer{};
    BitWriter writer(buffer.data(), buffer.size());
    for (int i = 0; i < 8; ++i) {
        writer.write_ranged(i % 8, 0, 7);
    }
    writer.flush();

    CHECK(writer.bytes_written() == 3);  // 8 values * 3 bits = 24 bits
}

TEST_CASE("values out of range are clamped rather than corrupting later fields") {
    std::array<std::uint8_t, 16> buffer{};
    BitWriter writer(buffer.data(), buffer.size());

    writer.write_ranged(9999, 0, 7);  // clamped to 7
    writer.write_bits(0b101, 3);      // must still read back intact
    writer.flush();

    BitReader reader(buffer.data(), writer.bytes_written());
    CHECK(reader.read_ranged(0, 7) == 7);
    CHECK(reader.read_bits(3) == 0b101U);
}

TEST_CASE("strings round trip and are truncated to their declared maximum") {
    std::array<std::uint8_t, 128> buffer{};
    BitWriter writer(buffer.data(), buffer.size());

    writer.write_string("hello", 24);
    writer.write_string("", 24);
    writer.write_string("this name is far too long to fit in the limit", 8);
    writer.flush();

    BitReader reader(buffer.data(), writer.bytes_written());
    CHECK(reader.read_string(24) == "hello");
    CHECK(reader.read_string(24).empty());
    CHECK(reader.read_string(8) == "this nam");
}

TEST_CASE("writing past the end latches overflow and never exceeds capacity") {
    // Run this suite under the `asan` preset to also prove no write lands outside
    // the buffer; here we assert the contract the caller relies on.
    std::array<std::uint8_t, 4> buffer{};

    BitWriter writer(buffer.data(), buffer.size());
    for (int i = 0; i < 100; ++i) {
        writer.write_bits(0xFFFFFFFFU, 32);
    }
    writer.flush();

    CHECK(writer.overflowed());
    CHECK(writer.bytes_written() <= buffer.size());
}

TEST_CASE("reading past the end latches overflow and yields zeros") {
    // This is the defence against a truncated or hostile packet: every read
    // structurally succeeds, and the caller rejects the message at the end.
    std::array<std::uint8_t, 2> buffer{};
    buffer[0] = 0xFF;
    buffer[1] = 0xFF;

    BitReader reader(buffer.data(), buffer.size());
    CHECK(reader.read_bits(16) == 0xFFFFU);
    CHECK_FALSE(reader.overflowed());

    CHECK(reader.read_bits(16) == 0U);
    CHECK(reader.overflowed());
}

TEST_CASE("a reader consumes exactly what the writer produced") {
    std::array<std::uint8_t, 64> buffer{};
    BitWriter writer(buffer.data(), buffer.size());
    writer.write_bits(3, 2);
    writer.write_bits(200, 8);
    writer.write_bits(1, 1);
    writer.flush();

    // 11 bits rounds up to 2 bytes.
    CHECK(writer.bytes_written() == 2);

    BitReader reader(buffer.data(), writer.bytes_written());
    CHECK(reader.read_bits(2) == 3U);
    CHECK(reader.read_bits(8) == 200U);
    CHECK(reader.read_bits(1) == 1U);
    CHECK_FALSE(reader.overflowed());
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Bit-level serialisation. Lives in core/, not net/, despite the wire format
// being its oldest and heaviest user: the content blob (sim/content_blob.hpp) is
// serialised with the same primitives, and sim/ may not include net/ —
// check-layering.sh forbids it, because net/ depends on sim and not the reverse.
// Nothing here knows about sockets or packets; it is bytes in, bytes out.

namespace core {

/// Bits needed to store any value in [0, max_value].
constexpr int bits_required(std::uint32_t max_value) {
    int bits = 0;
    while (max_value > 0) {
        ++bits;
        max_value >>= 1U;
    }
    return bits == 0 ? 1 : bits;
}

/// Writes fields at bit granularity into a caller-owned buffer.
///
/// A generic serialisation library would cost 4 bytes for a value with 8 possible
/// states; here a Direction costs 3 bits. At 10 snapshots a second times every
/// visible actor times every connected player, that difference is the bandwidth
/// bill. Overflow is sticky and silent-but-detectable: writes past the end are
/// dropped and overflowed() latches, so a caller checks once at the end instead
/// of after every field.
class BitWriter {
public:
    BitWriter(std::uint8_t* buffer, std::size_t capacity)
        : buffer_(buffer), capacity_(capacity) {}

    void write_bits(std::uint32_t value, int bits);
    void write_bool(bool value) { write_bits(value ? 1U : 0U, 1); }

    /// Writes only as many bits as the range needs. min/max are inclusive and
    /// must match exactly on the reading side.
    void write_ranged(std::int32_t value, std::int32_t min, std::int32_t max);

    void write_bytes(const void* data, std::size_t length);

    /// Length-prefixed, truncated to `max_length`. 6-bit length ⇒ 63 chars.
    void write_string(std::string_view text, std::size_t max_length = 63);

    /// Pads to the next byte boundary. Must be called before bytes_written().
    void flush();

    std::size_t bytes_written() const { return byte_pos_; }
    bool        overflowed() const { return overflow_; }

private:
    std::uint8_t* buffer_ = nullptr;
    std::size_t   capacity_ = 0;
    std::uint64_t scratch_ = 0;
    int           scratch_bits_ = 0;
    std::size_t   byte_pos_ = 0;
    bool          overflow_ = false;
};

/// Mirror of BitWriter. Reads past the end return 0 and latch overflowed(),
/// which is what makes a truncated or hostile packet safe to parse: every read
/// succeeds structurally and the caller rejects the whole message at the end.
class BitReader {
public:
    BitReader(const std::uint8_t* buffer, std::size_t size)
        : buffer_(buffer), size_(size) {}

    std::uint32_t read_bits(int bits);
    bool          read_bool() { return read_bits(1) != 0U; }
    std::int32_t  read_ranged(std::int32_t min, std::int32_t max);

    void        read_bytes(void* out, std::size_t length);
    std::string read_string(std::size_t max_length = 63);

    bool        overflowed() const { return overflow_; }
    std::size_t bytes_consumed() const { return byte_pos_; }

private:
    const std::uint8_t* buffer_ = nullptr;
    std::size_t         size_ = 0;
    std::uint64_t       scratch_ = 0;
    int                 scratch_bits_ = 0;
    std::size_t         byte_pos_ = 0;
    bool                overflow_ = false;
};

}  // namespace core

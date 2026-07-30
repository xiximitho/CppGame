#include "core/bitstream.hpp"

#include <algorithm>
#include <cstring>

namespace core {

void BitWriter::write_bits(std::uint32_t value, int bits) {
    if (bits <= 0 || bits > 32) {
        return;
    }
    if (bits < 32) {
        // Mask off anything the caller passed above the declared width, so a
        // stray high bit cannot corrupt the next field.
        value &= (1U << static_cast<unsigned>(bits)) - 1U;
    }

    scratch_ |= static_cast<std::uint64_t>(value) << static_cast<unsigned>(scratch_bits_);
    scratch_bits_ += bits;

    while (scratch_bits_ >= 8) {
        if (byte_pos_ >= capacity_) {
            overflow_ = true;
            scratch_ = 0;
            scratch_bits_ = 0;
            return;
        }
        buffer_[byte_pos_++] = static_cast<std::uint8_t>(scratch_ & 0xFFU);
        scratch_ >>= 8U;
        scratch_bits_ -= 8;
    }
}

void BitWriter::write_ranged(std::int32_t value, std::int32_t min,
                             std::int32_t max) {
    if (max <= min) {
        return;
    }
    const std::int32_t clamped = std::clamp(value, min, max);
    const auto span = static_cast<std::uint32_t>(max - min);
    write_bits(static_cast<std::uint32_t>(clamped - min), bits_required(span));
}

void BitWriter::write_bytes(const void* data, std::size_t length) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < length; ++i) {
        write_bits(bytes[i], 8);
    }
}

void BitWriter::write_string(std::string_view text, std::size_t max_length) {
    const std::size_t length = std::min(text.size(), max_length);
    write_bits(static_cast<std::uint32_t>(length),
               bits_required(static_cast<std::uint32_t>(max_length)));
    write_bytes(text.data(), length);
}

void BitWriter::flush() {
    if (scratch_bits_ <= 0) {
        return;
    }
    if (byte_pos_ >= capacity_) {
        overflow_ = true;
        return;
    }
    buffer_[byte_pos_++] = static_cast<std::uint8_t>(scratch_ & 0xFFU);
    scratch_ = 0;
    scratch_bits_ = 0;
}

std::uint32_t BitReader::read_bits(int bits) {
    if (bits <= 0 || bits > 32) {
        return 0;
    }

    while (scratch_bits_ < bits) {
        std::uint8_t byte = 0;
        if (byte_pos_ < size_) {
            byte = buffer_[byte_pos_++];
        } else {
            overflow_ = true;
        }
        scratch_ |= static_cast<std::uint64_t>(byte)
                    << static_cast<unsigned>(scratch_bits_);
        scratch_bits_ += 8;
    }

    const std::uint64_t mask =
        (bits == 32) ? 0xFFFFFFFFULL
                     : ((1ULL << static_cast<unsigned>(bits)) - 1ULL);
    const auto result = static_cast<std::uint32_t>(scratch_ & mask);

    scratch_ >>= static_cast<unsigned>(bits);
    scratch_bits_ -= bits;

    return result;
}

std::int32_t BitReader::read_ranged(std::int32_t min, std::int32_t max) {
    if (max <= min) {
        return min;
    }
    const auto span = static_cast<std::uint32_t>(max - min);
    const std::uint32_t raw = read_bits(bits_required(span));
    return min + static_cast<std::int32_t>(raw);
}

void BitReader::read_bytes(void* out, std::size_t length) {
    auto* bytes = static_cast<std::uint8_t*>(out);
    for (std::size_t i = 0; i < length; ++i) {
        bytes[i] = static_cast<std::uint8_t>(read_bits(8));
    }
}

std::string BitReader::read_string(std::size_t max_length) {
    const std::uint32_t length =
        read_bits(bits_required(static_cast<std::uint32_t>(max_length)));

    // The length came off the wire, so it is untrusted even though the bit width
    // bounds it: clamp before allocating.
    const std::size_t safe_length =
        std::min(static_cast<std::size_t>(length), max_length);

    std::string result;
    result.resize(safe_length);
    for (std::size_t i = 0; i < safe_length; ++i) {
        result[i] = static_cast<char>(read_bits(8));
    }
    return result;
}

}  // namespace core

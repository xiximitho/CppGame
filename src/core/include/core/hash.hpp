#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// FNV-1a, 64-bit.
//
// Chosen for what it is NOT: it is not cryptographic and nothing here pretends it
// is. The job is detecting that two byte sequences differ — a client and a server
// disagreeing about content, a cache key — where an adversary is not the threat and
// a five-line function with no dependency beats pulling in a hash library.
//
// It is specified by a fixed algorithm with fixed constants, so the same bytes give
// the same digest on every platform and compiler. That property is the whole point:
// a hash that differed between Linux and Windows would reject every cross-platform
// connection. Same reasoning as sim::Rng existing instead of <random>.

namespace core {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

/// Hashes `size` bytes. Passing a null pointer yields the offset basis, which is
/// the same answer as hashing nothing.
constexpr std::uint64_t fnv1a_64(const void* data, std::size_t size,
                                 std::uint64_t seed = kFnvOffsetBasis) {
    std::uint64_t hash = seed;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes == nullptr) {
        return hash;
    }
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

constexpr std::uint64_t fnv1a_64(std::string_view text,
                                 std::uint64_t seed = kFnvOffsetBasis) {
    std::uint64_t hash = seed;
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= kFnvPrime;
    }
    return hash;
}

}  // namespace core

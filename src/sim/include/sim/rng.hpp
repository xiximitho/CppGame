#pragma once

#include <cstdint>

namespace sim {

/// Deliberately not <random>: the standard distributions are not specified to
/// produce identical sequences across implementations, so a save file or a
/// generated map would differ between Linux and Windows. This is a fixed
/// algorithm (PCG-XSH-RR 64/32) that produces the same stream everywhere.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed + kIncrement) { next_u32(); }

    std::uint32_t next_u32() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + kIncrement;
        const auto xorshifted =
            static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
        const auto rot = static_cast<std::uint32_t>(old >> 59U);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1U) & 31U));
    }

    /// Uniform in [0, bound). Returns 0 when bound is 0.
    std::uint32_t next_below(std::uint32_t bound) {
        if (bound == 0) {
            return 0;
        }
        // Rejection sampling; unbiased, unlike a plain modulo.
        const std::uint32_t threshold = (~bound + 1U) % bound;
        for (;;) {
            const std::uint32_t r = next_u32();
            if (r >= threshold) {
                return r % bound;
            }
        }
    }

    /// Uniform in [min, max] inclusive.
    int next_range(int min, int max) {
        if (max <= min) {
            return min;
        }
        const auto span = static_cast<std::uint32_t>(max - min + 1);
        return min + static_cast<int>(next_below(span));
    }

    float next_float() {
        return static_cast<float>(next_u32() >> 8U) / 16777216.0F;
    }

    bool chance(float probability) { return next_float() < probability; }

private:
    static constexpr std::uint64_t kIncrement = 1442695040888963407ULL;
    std::uint64_t state_ = 0;
};

}  // namespace sim

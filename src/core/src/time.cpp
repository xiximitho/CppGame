#include "core/time.hpp"

#include <chrono>
#include <thread>

namespace core {
namespace {

using Clock = std::chrono::steady_clock;

const Clock::time_point& process_start() {
    static const Clock::time_point start = Clock::now();
    return start;
}

}  // namespace

std::uint64_t now_nanos() {
    const auto delta = Clock::now() - process_start();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count());
}

double uptime_seconds() {
    return static_cast<double>(now_nanos()) / 1'000'000'000.0;
}

void sleep_nanos(std::uint64_t nanos) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(nanos));
}

}  // namespace core

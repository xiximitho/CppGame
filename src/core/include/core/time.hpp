#pragma once

#include <cstdint>

namespace core {

/// Monotonic nanoseconds since an arbitrary epoch. Never goes backwards, never
/// affected by the wall clock being adjusted.
std::uint64_t now_nanos();

/// Monotonic seconds since the process started. Convenience for logs.
double uptime_seconds();

/// Sleeps at least `nanos`. Used by the server to pace its fixed tick; the
/// client paces on the display instead and does not call this.
void sleep_nanos(std::uint64_t nanos);

}  // namespace core

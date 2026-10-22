// PS Vita test harness shim for <rex/chrono/clock.h> (mftb / REX_QUERY_TIMEBASE).
// Guest timebase approximated as 50 MHz from the process clock (microseconds).
// The Xbox 360 timebase is 49.875 MHz; tests only need a monotonic value.
#pragma once

#include <cstdint>

extern "C" uint64_t vita_harness_time_us(void);

namespace rex::chrono {
struct Clock {
  static uint64_t QueryGuestTickCount() { return vita_harness_time_us() * 50u; }
};
}  // namespace rex::chrono

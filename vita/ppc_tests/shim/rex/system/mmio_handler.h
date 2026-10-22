// PS Vita test harness shim for <rex/system/mmio_handler.h>.
// The instruction tests do not touch MMIO; any MMIO access aborts with a
// message (see harness.cpp) instead of pretending to succeed.
#pragma once

#include <cstdint>

namespace rex::runtime {
class MMIOHandler {
 public:
  static MMIOHandler* global_handler();
  bool CheckLoad(uint32_t address, uint32_t* out_value);
  bool CheckStore(uint32_t address, uint32_t value);
};
}  // namespace rex::runtime

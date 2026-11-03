// Storm 2: blocking version of the D3D wait for a GPU fence
// (storm2_blocking_fence_wait).
//
// sub_826A5DC0(device, fence, ...) waits until the GPU has passed `fence`
// (verified in the recompiled code): while the fence value the GPU writes to
// memory, *(device + 10896), is still behind, it calls sub_826ADDE0(wait), one
// poll step: 32 db16cyc (an SMT hint, "lower my priority while I wait", a
// no-op in recompiled code), a few device checks and a GPU hang check after
// 5000 ms; it returns 0 to stop waiting. On the Xbox 360 that loop sat on a
// hardware thread of its own. Recompiled it is a busy loop on the game's
// render thread (~96% of a core on the PC); on the Vita that thread shares a
// core with the audio engine, and in Vita3K the spin takes host CPU from the
// threads that draw.
//
// Here each poll step runs as before; when it asks to keep waiting and the
// fence value did not move, the thread blocks until the GPU side writes guest
// memory again (rex::graphics::NotifyGpuWrite: fence writes, scratch
// writeback, interrupt callbacks) or 2 ms pass, and the caller checks again.

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/graphics/gpu_write_signal.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(storm2_blocking_fence_wait, true, "Storm 2",
                    "Block in the game's wait for a GPU fence (sub_826ADDE0, the step of "
                    "sub_826A5DC0's loop) until the GPU writes memory, instead of spinning");
REXCVAR_DEFINE_INT32(storm2_fence_wait_report, 0, "Storm 2",
                     "Seconds between log lines with the fence wait counts (0: off)")
    .range(0, 3600);

namespace {

uint32_t LoadBe32(uint8_t* base, uint32_t address) {
  uint32_t v = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    v = (v << 8) | *rex::memory::GuestPtr<const uint8_t*>(base, address + i);
  }
  return v;
}

std::atomic<uint64_t> g_steps{0}, g_waits{0}, g_timeouts{0}, g_wait_us{0};
std::atomic<int64_t> g_last_report_us{0};

int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void MaybeReport() {
  const int32_t seconds = REXCVAR_GET(storm2_fence_wait_report);
  if (seconds <= 0) {
    return;
  }
  const int64_t now = NowUs();
  int64_t last = g_last_report_us.load(std::memory_order_relaxed);
  if (!last) {
    g_last_report_us.compare_exchange_strong(last, now);
    return;
  }
  if (now - last < int64_t(seconds) * 1000000 ||
      !g_last_report_us.compare_exchange_strong(last, now)) {
    return;
  }
  const uint64_t steps = g_steps.exchange(0), waits = g_waits.exchange(0);
  const uint64_t timeouts = g_timeouts.exchange(0), wait_us = g_wait_us.exchange(0);
  REXLOG_INFO("Storm 2 fence wait: {} poll steps, {} blocked ({} ended by the 2 ms timeout), "
              "{:.0f} ms blocked in {:.1f} s",
              steps, waits, timeouts, double(wait_us) / 1000.0, double(now - last) / 1e6);
}

}  // namespace

REX_EXTERN(__imp__sub_826ADDE0);

REX_HOOK_RAW(sub_826ADDE0) {
  if (!REXCVAR_GET(storm2_blocking_fence_wait)) {
    __imp__sub_826ADDE0(ctx, base);
    return;
  }
  const uint32_t device = LoadBe32(base, ctx.r3.u32 + 0);
  const uint32_t fence_address = LoadBe32(base, device + 10896);
  // Read before the step, so a GPU write during it counts as a change below.
  const uint64_t sequence = rex::graphics::GpuWriteSequence();
  const uint32_t before = LoadBe32(base, fence_address);
  __imp__sub_826ADDE0(ctx, base);
  g_steps.fetch_add(1, std::memory_order_relaxed);
  if (ctx.r3.u32 != 0 && LoadBe32(base, fence_address) == before) {
    const int64_t start = NowUs();
    if (!rex::graphics::WaitGpuWrite(sequence, std::chrono::milliseconds(2))) {
      g_timeouts.fetch_add(1, std::memory_order_relaxed);
    }
    g_waits.fetch_add(1, std::memory_order_relaxed);
    g_wait_us.fetch_add(uint64_t(NowUs() - start), std::memory_order_relaxed);
  }
  MaybeReport();
}

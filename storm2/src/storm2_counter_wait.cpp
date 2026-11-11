// Storm 2: blocking version of the game's "wait until counter == value"
// (storm2_blocking_counter_wait).
//
// The game hands frames between the main thread and its render thread
// (F8000068) through an object { +0 mutex, +4 auto-reset event, +16 u64
// counter } (verified in the recompiled code):
//   sub_825D5AA8(obj, v): lock; counter = v; unlock; SetEvent(event).
//   sub_825D5B00(obj, v): lock; while (counter != v) { SignalObjectAndWait(
//     mutex, event) (unlock + wait); lock; if (counter == v) break;
//     SetEvent(event) (pass the wake-up on to other waiters) }; unlock.
// The render thread's loop (sub_825D2A00) waits for 1 (a frame posted by the
// main thread), processes it and sets 0; the main thread waits for 0. A waiter
// whose value is not there yet sets the event again and then waits on it, so
// with nobody else waiting it wakes itself at once: a spin through kernel calls
// while the other thread works (~760 thousand calls per second of each of
// SignalAndWait, Wait and SetEvent on the PC, measured with kernel_sync_stats).
// On the Xbox 360 the render thread had a hardware thread of its own; on the
// Vita (and in Vita3K, where each kernel call is emulated) it takes the CPU
// from the other threads.
//
// Here the wait reads the counter under the same guest mutex, through the
// game's own wrappers, and in between blocks on a host condition that
// sub_825D5AA8 wakes. The guest event is no longer waited on by these waiters
// (it stays signalled; no other code waits on it in the runs measured). A
// 2 ms timeout re-checks the counter in case other code changes it.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "storm2_probe.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/platform.h>
#include <rex/system/xmemory.h>

#if REX_PLATFORM_VITA
#include <atomic>
#include <psp2/kernel/threadmgr.h>
#endif

REXCVAR_DEFINE_BOOL(storm2_blocking_counter_wait, true, "Storm 2",
                    "Block in the game's counter wait (sub_825D5B00, the hand-off between the "
                    "main and the render thread) instead of its loop, which wakes itself through "
                    "kernel calls while the other thread works");

namespace {

uint32_t LoadBe32(uint8_t* base, uint32_t address) {
  uint32_t v = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    v = (v << 8) | *rex::memory::GuestPtr<const uint8_t*>(base, address + i);
  }
  return v;
}

uint64_t LoadBe64(uint8_t* base, uint32_t address) {
  return (uint64_t(LoadBe32(base, address)) << 32) | LoadBe32(base, address + 4);
}

// One per guest object (a handful in the game; never freed).
#if REX_PLATFORM_VITA
// On a kernel semaphore: std::condition_variable is pthread-embedded's there,
// whose timed waits returned at once on a console (threading_vita.cpp,
// VitaCond), so this wait spun (~125 thousand mutex locks a second) on the
// main thread and the render thread, which the menu's CPU time then showed.
// notify signals as many times as threads may be blocked; a wake that finds
// nothing changed re-checks and blocks again.
struct HostWait {
  std::atomic<uint64_t> generation{0};
  std::atomic<uint32_t> waiters{0};
  SceUID sema = sceKernelCreateSema("storm2_counter", 0, 0, 64, nullptr);
  void Notify() {
    generation.fetch_add(1, std::memory_order_seq_cst);
    const uint32_t n = waiters.load(std::memory_order_seq_cst);
    if (n && sema >= 0) {
      sceKernelSignalSema(sema, int(std::min<uint32_t>(n, 64)));
    }
  }
  uint64_t Generation() const { return generation.load(std::memory_order_seq_cst); }
  // Blocks until the generation moves past `seen` or `timeout_us` passes.
  void WaitChange(uint64_t seen, uint32_t timeout_us) {
    waiters.fetch_add(1, std::memory_order_seq_cst);
    if (generation.load(std::memory_order_seq_cst) == seen) {
      if (sema >= 0) {
        SceUInt t = timeout_us;
        sceKernelWaitSema(sema, 1, &t);
      } else {
        sceKernelDelayThread(timeout_us);
      }
    }
    waiters.fetch_sub(1, std::memory_order_seq_cst);
  }
};
#else
struct HostWait {
  std::mutex mutex;
  std::condition_variable changed;
  uint64_t generation = 0;  // bumped by every sub_825D5AA8 on the object
  void Notify() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      ++generation;
    }
    changed.notify_all();
  }
  uint64_t Generation() {
    std::lock_guard<std::mutex> lock(mutex);
    return generation;
  }
  void WaitChange(uint64_t seen, uint32_t timeout_us) {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock, std::chrono::microseconds(timeout_us),
                     [&] { return generation != seen; });
  }
};
#endif

std::mutex g_waits_mutex;
std::unordered_map<uint32_t, HostWait*> g_waits;

HostWait& WaitFor(uint32_t object) {
  std::lock_guard<std::mutex> lock(g_waits_mutex);
  HostWait*& w = g_waits[object];
  if (!w) {
    w = new HostWait();
  }
  return *w;
}

}  // namespace

REX_EXTERN(__imp__sub_825D5AA8);
REX_EXTERN(__imp__sub_825D5B00);
REX_EXTERN(sub_82563E00);  // WaitForSingleObject(r3 = handle, r4 = timeout in ms)
REX_EXTERN(sub_826938D8);  // ReleaseMutex(r3 = handle)

REX_HOOK_RAW(sub_825D5AA8) {
  const uint32_t object = ctx.r3.u32;
  __imp__sub_825D5AA8(ctx, base);
  WaitFor(object).Notify();
}

REX_HOOK_RAW(sub_825D5B00) {
  storm2_probe::Scope probe(storm2_probe::kCounterWait);
  if (!REXCVAR_GET(storm2_blocking_counter_wait)) {
    __imp__sub_825D5B00(ctx, base);
    return;
  }
  const uint32_t object = ctx.r3.u32;
  const uint64_t value = ctx.r4.u64;
  const uint32_t guest_mutex = LoadBe32(base, object + 0);
  HostWait& w = WaitFor(object);
  for (;;) {
    // Read before the check, so a set between the check and the wait below
    // is seen as a changed generation.
    const uint64_t generation = w.Generation();
    ctx.r3.u64 = guest_mutex;
    ctx.r4.s64 = -1;  // INFINITE, as the original
    sub_82563E00(ctx, base);
    const bool reached = LoadBe64(base, object + 16) == value;
    ctx.r3.u64 = guest_mutex;
    sub_826938D8(ctx, base);  // r3 = its result, as the original returns
    if (reached) {
      return;
    }
    w.WaitChange(generation, 2000);
  }
}

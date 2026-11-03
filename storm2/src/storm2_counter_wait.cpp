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

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/xmemory.h>

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
struct HostWait {
  std::mutex mutex;
  std::condition_variable changed;
  uint64_t generation = 0;  // bumped by every sub_825D5AA8 on the object
};

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
  HostWait& w = WaitFor(object);
  {
    std::lock_guard<std::mutex> lock(w.mutex);
    ++w.generation;
  }
  w.changed.notify_all();
}

REX_HOOK_RAW(sub_825D5B00) {
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
    uint64_t generation;
    {
      std::lock_guard<std::mutex> lock(w.mutex);
      generation = w.generation;
    }
    ctx.r3.u64 = guest_mutex;
    ctx.r4.s64 = -1;  // INFINITE, as the original
    sub_82563E00(ctx, base);
    const bool reached = LoadBe64(base, object + 16) == value;
    ctx.r3.u64 = guest_mutex;
    sub_826938D8(ctx, base);  // r3 = its result, as the original returns
    if (reached) {
      return;
    }
    std::unique_lock<std::mutex> lock(w.mutex);
    w.changed.wait_for(lock, std::chrono::milliseconds(2),
                       [&] { return w.generation != generation; });
  }
}

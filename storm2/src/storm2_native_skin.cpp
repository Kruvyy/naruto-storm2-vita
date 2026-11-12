// Storm 2: native version of the battle's vertex blend kernel (sub_827C3850).
//
// The game's job threads (entry 825CE518) spend nearly all their busy time
// in this function during a battle (PS Vita console, LR sampler, 2026-11-12):
// for each vertex it blends four bones' 4x4 matrices from two tables by four
// weights (lvx128/vspltw/vmulfp128/vmaddfp, 120 guest instructions) and
// writes two vectors. The main thread waited 45-125 ms per frame for those
// jobs. The recompiled code turns it into ~1150 ARM instructions: two vtbl
// and a mask load for every lvx, and every vector result stored to
// PPCContext. scripts/ppc_to_native.py writes the native version at build
// time (VMX registers as NEON locals in guest element order, storm2_native.h);
// its results are the same as the recompiled code's (same multiplies and
// adds in the same order), which storm2_native_skin_check verifies.

#include "storm2_native.h"

#include <atomic>
#include <chrono>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(storm2_native_skin, true, "Storm 2",
                    "Run the native version of the battle's vertex blend kernel (sub_827C3850, "
                    "the job threads' work) instead of its recompiled code (same results; read "
                    "at every call, so a diagnostic A/B can switch it)");
REXCVAR_DEFINE_BOOL(storm2_native_skin_check, false, "Storm 2",
                    "Diagnostic: run the recompiled and the native kernel on every call from the "
                    "same memory and log how many output bytes differ");
REXCVAR_DEFINE_INT32(storm2_native_skin_stats, 0, "Storm 2",
                     "Log calls, vertices and time per call of the vertex blend kernel every N "
                     "seconds (0 = off)");

#include "sub_827C3850.inc"

namespace {

using Clock = std::chrono::steady_clock;

struct ModeStats {
  std::atomic<uint64_t> calls{0}, vertices{0}, ns{0};
};
ModeStats g_stats[2];  // [0] recompiled, [1] native
std::atomic<uint64_t> g_checked{0}, g_differing_calls{0}, g_differing_bytes{0};
std::atomic<int64_t> g_last_log_ns{0};

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
      .count();
}

void CopyGuest(uint8_t* base, uint32_t address, uint32_t size, uint8_t* out, bool to_guest) {
  uint32_t done = 0;
  while (done < size) {
    const uint32_t a = address + done;
    const uint32_t chunk = std::min(size - done, 0x1000u - (a & 0xFFFu));
    uint8_t* host = storm2_native::Host(base, a);
    if (to_guest) {
      std::memcpy(host, out + done, chunk);
    } else {
      std::memcpy(out + done, host, chunk);
    }
    done += chunk;
  }
}

void MaybeLog() {
  const int32_t interval = REXCVAR_GET(storm2_native_skin_stats);
  if (interval <= 0) {
    return;
  }
  const int64_t now = NowNs();
  int64_t last = g_last_log_ns.load(std::memory_order_relaxed);
  if (last == 0) {
    g_last_log_ns.compare_exchange_strong(last, now);
    return;
  }
  if (now - last < int64_t(interval) * 1000000000ll ||
      !g_last_log_ns.compare_exchange_strong(last, now)) {
    return;
  }
  const double seconds = double(now - last) / 1e9;
  for (int mode = 0; mode < 2; ++mode) {
    const uint64_t calls = g_stats[mode].calls.exchange(0);
    const uint64_t vertices = g_stats[mode].vertices.exchange(0);
    const uint64_t ns = g_stats[mode].ns.exchange(0);
    if (calls) {
      REXLOG_INFO("Storm 2 vertex blend ({}): {} calls, {} vertices in {:.1f} s; {:.1f} ms per s "
                  "on all threads, {:.2f} us per call, {:.3f} us per vertex",
                  mode ? "native" : "recompiled", calls, vertices, seconds,
                  double(ns) / 1e6 / seconds, double(ns) / 1e3 / double(calls),
                  vertices ? double(ns) / 1e3 / double(vertices) : 0.0);
    }
  }
  if (const uint64_t checked = g_checked.exchange(0)) {
    REXLOG_INFO("Storm 2 vertex blend check: {} calls compared, {} differed ({} bytes)", checked,
                g_differing_calls.exchange(0), g_differing_bytes.exchange(0));
  }
}

}  // namespace

REX_EXTERN(__imp__sub_827C3850);

REX_HOOK_RAW(sub_827C3850) {
  // Job record (r3): +16 output (32 bytes per vertex), +24 vertex count.
  const uint32_t job = ctx.r3.u32;
  const uint32_t out = storm2_native::Load32(base, job + 16);
  const int32_t count = int32_t(storm2_native::Load32(base, job + 24));
  const bool native = REXCVAR_GET(storm2_native_skin);
  const bool stats = REXCVAR_GET(storm2_native_skin_stats) > 0;

  if (REXCVAR_GET(storm2_native_skin_check) && count > 0) {
    const uint32_t size = uint32_t(count) * 32u;
    std::vector<uint8_t> before(size), after_original(size), after_native(size);
    CopyGuest(base, out, size, before.data(), false);
    const PPCContext entry = ctx;
    __imp__sub_827C3850(ctx, base);
    CopyGuest(base, out, size, after_original.data(), false);
    CopyGuest(base, out, size, before.data(), true);
    PPCContext native_ctx = entry;
    Native_sub_827C3850(native_ctx, base);
    CopyGuest(base, out, size, after_native.data(), false);
    // Leave the recompiled code's result in guest memory.
    CopyGuest(base, out, size, after_original.data(), true);
    uint64_t differing = 0;
    for (uint32_t i = 0; i < size; ++i) {
      differing += after_original[i] != after_native[i];
    }
    g_checked.fetch_add(1, std::memory_order_relaxed);
    if (differing) {
      g_differing_calls.fetch_add(1, std::memory_order_relaxed);
      g_differing_bytes.fetch_add(differing, std::memory_order_relaxed);
    }
    MaybeLog();
    return;
  }

  const int64_t start = stats ? NowNs() : 0;
  if (native) {
    Native_sub_827C3850(ctx, base);
  } else {
    __imp__sub_827C3850(ctx, base);
  }
  if (stats) {
    ModeStats& s = g_stats[native ? 1 : 0];
    s.calls.fetch_add(1, std::memory_order_relaxed);
    s.vertices.fetch_add(count > 0 ? uint64_t(count) : 0, std::memory_order_relaxed);
    s.ns.fetch_add(uint64_t(NowNs() - start), std::memory_order_relaxed);
    MaybeLog();
  }
}

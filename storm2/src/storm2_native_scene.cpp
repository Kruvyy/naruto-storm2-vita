// Storm 2: native version of sub_82638CB8, which brings the world matrices of
// a chain of scene nodes up to date (walks to the first dirty parent, then
// multiplies each node's local matrix by its parent's: sub_825C9A38, 4x4).
//
// PS Vita console, LR sampler, Free Battle (benchmark 8b, 2026-11-13): ~5% of
// the main thread's samples were in it, most at its matrix multiply and store.
// scripts/ppc_to_native.py writes the native version at build time with the
// small matrix leaves inlined (825C9A38 multiply, 825CA2F8 store, 825CA318
// load, 825C9A10 identity): VMX registers as NEON locals, the same operations
// in the same order. storm2_native_scene_check verifies it: the function also
// clears each node's dirty flag, so the check puts the bytes it wrote back
// (undo log) before running the other version.

// The generated precompiled header (REX_ macros used by the native code).
#include "storm2_pch.h"

#include "storm2_native.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(storm2_native_scene, true, "Storm 2",
                    "Run the native version of sub_82638CB8 (scene node world matrices, main "
                    "thread) instead of its recompiled code (same results; read at every call, "
                    "so a diagnostic A/B can switch it)");
REXCVAR_DEFINE_BOOL(storm2_native_scene_check, false, "Storm 2",
                    "Diagnostic: run sub_82638CB8 recompiled and native from the same memory and "
                    "registers and log how many bytes it writes and registers it leaves differ");
REXCVAR_DEFINE_INT32(storm2_native_scene_stats, 0, "Storm 2",
                     "Log calls and time per call of sub_82638CB8 every N seconds (0 = off)");

#include "sub_82638CB8.inc"

namespace {

using Clock = std::chrono::steady_clock;
using storm2_native::CopyRanges;
using storm2_native::MergeStores;
using storm2_native::RangeAddress;
using storm2_native::Ranges;

struct ModeStats {
  std::atomic<uint64_t> calls{0}, ns{0};
};
ModeStats g_stats[2];  // [0] recompiled, [1] native
std::atomic<uint64_t> g_checked{0}, g_checked_bytes{0}, g_differing_calls{0},
    g_differing_bytes{0}, g_differing_regs{0};
std::atomic<int64_t> g_last_log_ns{0};

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
      .count();
}

void MaybeLog() {
  const int32_t interval = REXCVAR_GET(storm2_native_scene_stats);
  if (interval <= 0 && !REXCVAR_GET(storm2_native_scene_check)) {
    return;
  }
  const int64_t now = NowNs();
  int64_t last = g_last_log_ns.load(std::memory_order_relaxed);
  if (last == 0) {
    g_last_log_ns.compare_exchange_strong(last, now);
    return;
  }
  const int64_t every = int64_t(interval > 0 ? interval : 10) * 1000000000ll;
  if (now - last < every || !g_last_log_ns.compare_exchange_strong(last, now)) {
    return;
  }
  const double seconds = double(now - last) / 1e9;
  for (int mode = 0; mode < 2; ++mode) {
    const uint64_t calls = g_stats[mode].calls.exchange(0);
    const uint64_t ns = g_stats[mode].ns.exchange(0);
    if (calls) {
      REXLOG_INFO("Storm 2 scene matrices sub_82638CB8 ({}): {} calls in {:.1f} s; {:.1f} ms per "
                  "s, {:.2f} us per call",
                  mode ? "native" : "recompiled", calls, seconds, double(ns) / 1e6 / seconds,
                  double(ns) / 1e3 / double(calls));
    }
  }
  if (const uint64_t checked = g_checked.exchange(0)) {
    REXLOG_INFO("Storm 2 scene matrices check: {} calls compared ({} bytes written), {} "
                "differed: {} bytes, {} registers",
                checked, g_checked_bytes.exchange(0), g_differing_calls.exchange(0),
                g_differing_bytes.exchange(0), g_differing_regs.exchange(0));
  }
}

uint32_t Gpr(const PPCContext& c, int n) {
  const PPCRegister* r[] = {&c.r0,  &c.r1,  &c.r2,  &c.r3,  &c.r4,  &c.r5,  &c.r6,  &c.r7,
                            &c.r8,  &c.r9,  &c.r10, &c.r11, &c.r12, &c.r13, &c.r14, &c.r15,
                            &c.r16, &c.r17, &c.r18, &c.r19, &c.r20, &c.r21, &c.r22, &c.r23,
                            &c.r24, &c.r25, &c.r26, &c.r27, &c.r28, &c.r29, &c.r30, &c.r31};
  return r[n]->u32;
}

}  // namespace

REX_EXTERN(__imp__sub_82638CB8);

REX_HOOK_RAW(sub_82638CB8) {
  const bool native = REXCVAR_GET(storm2_native_scene);
  const bool stats = REXCVAR_GET(storm2_native_scene_stats) > 0;

  if (REXCVAR_GET(storm2_native_scene_check)) {
    // 1. Native, tracked with the bytes each store replaces; then undone.
    const PPCContext entry = ctx;
    storm2_native::StoreTrack track;
    track.exclude_lo = ctx.r1.u32 - 0x10000u;
    track.exclude_hi = ctx.r1.u32;
    track.keep_old = true;
    storm2_native::tls_store_track = &track;
    PPCContext tracked_ctx = entry;
    NativeT_sub_82638CB8<true>(tracked_ctx, base);
    storm2_native::tls_store_track = nullptr;
    storm2_native::UndoStores(base, track);
    const Ranges ranges = MergeStores(track.stores);
    // 2. Recompiled from the same state; 3. undone again, native.
    std::vector<uint8_t> after_original, after_native;
    __imp__sub_82638CB8(ctx, base);
    const PPCContext original_ctx = ctx;
    CopyRanges(base, ranges, after_original, false);
    storm2_native::UndoStores(base, track);
    PPCContext native_ctx = entry;
    Native_sub_82638CB8(native_ctx, base);
    CopyRanges(base, ranges, after_native, false);
    // Leave the recompiled code's result.
    CopyRanges(base, ranges, after_original, true);
    ctx = original_ctx;
    uint64_t differing = 0;
    std::string bytes;
    for (size_t i = 0; i < after_original.size(); ++i) {
      if (after_original[i] != after_native[i]) {
        ++differing;
        if (bytes.size() < 300) {
          bytes += fmt::format(" {:08X}:{:02X}/{:02X}", RangeAddress(ranges, i), after_original[i],
                               after_native[i]);
        }
      }
    }
    uint64_t regs = 0;
    std::string reg_names;
    for (int n = 0; n < 32; ++n) {
      if (n != 0 && !(n >= 5 && n <= 11) && Gpr(original_ctx, n) != Gpr(native_ctx, n)) {
        ++regs;
        reg_names += fmt::format(" r{} {:08X}/{:08X}", n, Gpr(original_ctx, n), Gpr(native_ctx, n));
      }
    }
    for (int i = 0; i < 128; ++i) {
      if (std::memcmp(&original_ctx.v0 + i, &native_ctx.v0 + i, sizeof(PPCVRegister)) != 0) {
        ++regs;
        if (reg_names.size() < 200) reg_names += fmt::format(" v{}", i);
      }
    }
    g_checked.fetch_add(1, std::memory_order_relaxed);
    g_checked_bytes.fetch_add(after_original.size(), std::memory_order_relaxed);
    if (differing || regs) {
      if (g_differing_calls.fetch_add(1, std::memory_order_relaxed) < 2) {
        REXLOG_WARN("Storm 2 scene matrices check: r3={:08X} r4={:08X}: {} of {} bytes and {} "
                    "registers differ;{};{}",
                    entry.r3.u32, entry.r4.u32, differing, after_original.size(), regs, reg_names,
                    bytes);
      }
      g_differing_bytes.fetch_add(differing, std::memory_order_relaxed);
      g_differing_regs.fetch_add(regs, std::memory_order_relaxed);
    }
    MaybeLog();
    return;
  }

  const int64_t start = stats ? NowNs() : 0;
  if (native) {
    Native_sub_82638CB8(ctx, base);
  } else {
    __imp__sub_82638CB8(ctx, base);
  }
  if (stats) {
    ModeStats& s = g_stats[native ? 1 : 0];
    s.calls.fetch_add(1, std::memory_order_relaxed);
    s.ns.fetch_add(uint64_t(NowNs() - start), std::memory_order_relaxed);
    MaybeLog();
  }
}

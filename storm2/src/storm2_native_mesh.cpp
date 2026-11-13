// Storm 2: native version of sub_82654128, a main-thread function of the
// battle that builds bone matrices and transforms vertices with them.
//
// PS Vita console, LR sampler, Free Battle (2026-11-12, bench_battle.txt):
// ~25% of the main thread's samples were in its per-vertex loop (task
// 8252B1B0, 16-135 ms per frame depending on what is on screen). Each vertex
// calls four small vector functions (825CA318 loads a 4x4 matrix into
// v92-v95, 825CA820 transforms a point with a perspective divide, 827B2400
// multiplies two vectors), and the recompiled code moves every vector through
// PPCContext with 16-byte table shuffles. scripts/ppc_to_native.py writes the
// native version at build time with those leaves inlined, VMX registers as
// NEON locals, the stack frame in guest memory and every register kept in step
// with PPCContext at its calls (virtual getters, an allocator, a sort, a 3x3
// inverse) and at its exit. The arithmetic is the recompiled code's (same
// operations in the same order; vrefp through the same rex_vrefp), which
// storm2_native_mesh_check verifies.

// The generated precompiled header: REX_CALL_INDIRECT_FUNC for the virtual calls.
#include "storm2_pch.h"

#include "storm2_native.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(storm2_native_mesh, true, "Storm 2",
                    "Run the native version of sub_82654128 (bone matrices and per-vertex "
                    "transforms on the main thread in a battle) instead of its recompiled code "
                    "(same results; read at every call, so a diagnostic A/B can switch it)");
REXCVAR_DEFINE_BOOL(storm2_native_mesh_check, false, "Storm 2",
                    "Diagnostic: run sub_82654128 recompiled and native from the same state and "
                    "log how many bytes it writes and registers it leaves differ");
REXCVAR_DEFINE_INT32(storm2_native_mesh_stats, 0, "Storm 2",
                     "Log calls and time per call of sub_82654128 every N seconds (0 = off)");

#include "sub_82654128.inc"

namespace {

using Clock = std::chrono::steady_clock;

struct ModeStats {
  std::atomic<uint64_t> calls{0}, ns{0};
};
ModeStats g_stats[2];  // [0] recompiled, [1] native
std::atomic<uint64_t> g_checked{0}, g_differing_calls{0}, g_differing_bytes{0},
    g_differing_regs{0}, g_checked_bytes{0}, g_differing_output_bytes{0}, g_differing_volatile{0};
std::atomic<int64_t> g_last_log_ns{0};

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
      .count();
}

void MaybeLog() {
  const int32_t interval = REXCVAR_GET(storm2_native_mesh_stats);
  const bool check = REXCVAR_GET(storm2_native_mesh_check);
  if (interval <= 0 && !check) {
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
      REXLOG_INFO("Storm 2 mesh transform sub_82654128 ({}): {} calls in {:.1f} s; {:.1f} ms per "
                  "s, {:.1f} us per call",
                  mode ? "native" : "recompiled", calls, seconds, double(ns) / 1e6 / seconds,
                  double(ns) / 1e3 / double(calls));
    }
  }
  if (const uint64_t checked = g_checked.exchange(0)) {
    REXLOG_INFO("Storm 2 mesh transform check: {} calls compared ({} bytes written), {} differed: "
                "{} bytes ({} of them at or above r4, the output), {} registers ({} of them "
                "r0/r5-r11, volatile)",
                checked, g_checked_bytes.exchange(0), g_differing_calls.exchange(0),
                g_differing_bytes.exchange(0), g_differing_output_bytes.exchange(0),
                g_differing_regs.exchange(0), g_differing_volatile.exchange(0));
  }
}

// Registers the two versions leave: GPRs (low 32 bits; the native function
// keeps 32-bit values), FPRs and VMX registers. `names` gets the first ones.
uint64_t DifferingRegisters(const PPCContext& a, const PPCContext& b, std::string* names) {
  static_assert(offsetof(PPCContext, f31) - offsetof(PPCContext, f0) == 31 * sizeof(PPCRegister));
  static_assert(offsetof(PPCContext, v127) - offsetof(PPCContext, v0) ==
                127 * sizeof(PPCVRegister));
  uint64_t n = 0;
  const PPCRegister* ga[] = {&a.r0,  &a.r1,  &a.r2,  &a.r3,  &a.r4,  &a.r5,  &a.r6,  &a.r7,
                             &a.r8,  &a.r9,  &a.r10, &a.r11, &a.r12, &a.r13, &a.r14, &a.r15,
                             &a.r16, &a.r17, &a.r18, &a.r19, &a.r20, &a.r21, &a.r22, &a.r23,
                             &a.r24, &a.r25, &a.r26, &a.r27, &a.r28, &a.r29, &a.r30, &a.r31};
  int index = 0;
  for (const PPCRegister* r : ga) {
    const size_t off = reinterpret_cast<const uint8_t*>(r) - reinterpret_cast<const uint8_t*>(&a);
    const PPCRegister* rb =
        reinterpret_cast<const PPCRegister*>(reinterpret_cast<const uint8_t*>(&b) + off);
    if (r->u32 != rb->u32) {
      ++n;
      if (names && names->size() < 300) {
        *names += fmt::format(" r{} {:08X}/{:08X}", index, r->u32, rb->u32);
      }
    }
    ++index;
  }
  for (int i = 0; i < 32; ++i) {
    if (std::memcmp(&a.f0 + i, &b.f0 + i, sizeof(PPCRegister)) != 0) {
      ++n;
      if (names && names->size() < 300) {
        *names += fmt::format(" f{} {:016X}/{:016X}", i, (&a.f0 + i)->u64, (&b.f0 + i)->u64);
      }
    }
  }
  for (int i = 0; i < 128; ++i) {
    if (std::memcmp(&a.v0 + i, &b.v0 + i, sizeof(PPCVRegister)) != 0) {
      ++n;
      if (names && names->size() < 300) {
        const uint32_t* x = (&a.v0 + i)->u32;
        const uint32_t* y = (&b.v0 + i)->u32;
        *names += fmt::format(" v{} {:08X}{:08X}{:08X}{:08X}/{:08X}{:08X}{:08X}{:08X}", i, x[3],
                              x[2], x[1], x[0], y[3], y[2], y[1], y[0]);
      }
    }
  }
  return n;
}

}  // namespace

using storm2_native::CopyRanges;
using storm2_native::MergeStores;
using storm2_native::RangeAddress;
using storm2_native::Ranges;

REX_EXTERN(__imp__sub_82654128);

REX_HOOK_RAW(sub_82654128) {
  const bool native = REXCVAR_GET(storm2_native_mesh);
  const bool stats = REXCVAR_GET(storm2_native_mesh_stats) > 0;

  if (REXCVAR_GET(storm2_native_mesh_check)) {
    // 1. The native version once with its stores tracked: the bytes it writes
    //    itself (outside its own stack frame; bytes only a callee writes, such
    //    as the never-initialised word of each entry of its temporary sort
    //    array, are not compared). It reads the source vertices and matrices
    //    and writes other memory, so running it again from the same registers
    //    gives the same result.
    const PPCContext entry = ctx;
    storm2_native::StoreTrack track;
    track.exclude_lo = ctx.r1.u32 - 0x10000u;
    track.exclude_hi = ctx.r1.u32;
    storm2_native::tls_store_track = &track;
    PPCContext tracked_ctx = entry;
    NativeT_sub_82654128<true>(tracked_ctx, base);
    storm2_native::tls_store_track = nullptr;
    const Ranges ranges = MergeStores(std::move(track.stores));
    // 2. The recompiled code, then 3. the native code, each from the entry state.
    std::vector<uint8_t> after_original, after_native;
    __imp__sub_82654128(ctx, base);
    const PPCContext original_ctx = ctx;
    CopyRanges(base, ranges, after_original, false);
    PPCContext native_ctx = entry;
    Native_sub_82654128(native_ctx, base);
    CopyRanges(base, ranges, after_native, false);
    // Leave the recompiled code's result in the output (at or above r4). Not
    // below it: those are the temporary arrays, already freed, and writing
    // the old bytes back there overwrote the heap's free-list links (the next
    // free spun forever in sub_82566DF8, Vita3K 2026-11-12).
    {
      Ranges output;
      std::vector<uint8_t> output_bytes;
      size_t at = 0;
      for (const auto& [start, end] : ranges) {
        if (start >= entry.r4.u32) {
          output.emplace_back(start, end);
          output_bytes.insert(output_bytes.end(), after_original.begin() + at,
                              after_original.begin() + at + (end - start));
        }
        at += end - start;
      }
      CopyRanges(base, output, output_bytes, true);
    }
    ctx = original_ctx;
    uint64_t differing = 0, differing_output = 0;
    std::string blocks;  // the first differing bytes: address original/native
    for (size_t i = 0; i < after_original.size(); ++i) {
      if (after_original[i] != after_native[i]) {
        ++differing;
        // Below r4: the temporary arrays it allocates, sorts and frees (the
        // native run gets them at another heap address).
        differing_output += RangeAddress(ranges, i) >= entry.r4.u32;
        if (blocks.size() < 600) {
          blocks += fmt::format(" {:08X}:{:02X}/{:02X}", RangeAddress(ranges, i),
                                after_original[i], after_native[i]);
        }
      }
    }
    std::string reg_names;
    const uint64_t regs = DifferingRegisters(original_ctx, native_ctx, &reg_names);
    g_checked.fetch_add(1, std::memory_order_relaxed);
    g_checked_bytes.fetch_add(after_original.size(), std::memory_order_relaxed);
    if (differing || regs) {
      if (g_differing_calls.fetch_add(1, std::memory_order_relaxed) < 2) {
        REXLOG_WARN("Storm 2 mesh transform check: r1={:08X} r3={:08X} r4={:08X} r5={:08X} "
                    "r6={:08X} r7={:08X}: {} of {} bytes and {} registers differ;{}; blocks{}",
                    entry.r1.u32, entry.r3.u32, entry.r4.u32, entry.r5.u32, entry.r6.u32,
                    entry.r7.u32, differing, after_original.size(), regs, reg_names, blocks);
      }
      g_differing_bytes.fetch_add(differing, std::memory_order_relaxed);
      g_differing_output_bytes.fetch_add(differing_output, std::memory_order_relaxed);
      // Volatile registers as the last callee (the allocator's free) leaves
      // them depend on the heap state, which the two runs leave differently.
      uint64_t volatile_regs = 0;
      for (const PPCRegister* a : {&original_ctx.r0, &original_ctx.r5, &original_ctx.r6,
                                   &original_ctx.r7, &original_ctx.r8, &original_ctx.r9,
                                   &original_ctx.r10, &original_ctx.r11}) {
        const size_t off = reinterpret_cast<const uint8_t*>(a) -
                           reinterpret_cast<const uint8_t*>(&original_ctx);
        volatile_regs += a->u32 != reinterpret_cast<const PPCRegister*>(
                                       reinterpret_cast<const uint8_t*>(&native_ctx) + off)->u32;
      }
      g_differing_volatile.fetch_add(volatile_regs, std::memory_order_relaxed);
      g_differing_regs.fetch_add(regs, std::memory_order_relaxed);
    }
    MaybeLog();
    return;
  }

  const int64_t start = stats ? NowNs() : 0;
  if (native) {
    Native_sub_82654128(ctx, base);
  } else {
    __imp__sub_82654128(ctx, base);
  }
  if (stats) {
    ModeStats& s = g_stats[native ? 1 : 0];
    s.calls.fetch_add(1, std::memory_order_relaxed);
    s.ns.fetch_add(uint64_t(NowNs() - start), std::memory_order_relaxed);
    MaybeLog();
  }
}

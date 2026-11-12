// Storm 2: native version of sub_825DD778, which copies a range of 4x4 bone
// matrices into a block it allocates and writes their inverses after them.
//
// PS Vita console, LR sampler, Free Battle (2026-11-12, console benchmark 2):
// ~6.5% of the main thread's samples were in its loop, which calls
// sub_825C9F20 (3x3 inverse of the matrix in v92-v94: cofactors, a dot
// product, vrefp with two Newton steps, selects for a zero or denormal
// determinant) once per matrix. The recompiled code moves every vector of
// both through PPCContext. scripts/ppc_to_native.py writes the native version
// at build time with sub_825C9F20 inlined (VMX registers as NEON locals, the
// same operations in the same order; the dot product through the recompiled
// code's own simde_mm_dp_ps), which storm2_native_inverse_check verifies.
//
// sub_825DD778(r3 = object, r4 = source matrices, r5 = first, r6 = last):
// stores r5/r6 at +28/+30, allocates (r6 - r5 + 1) * 128 bytes from the
// frame allocator at 0x82956900 and stores the block at +20 (copies) and
// block + n * 64 at +24 (inverses).

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

REXCVAR_DEFINE_BOOL(storm2_native_inverse, true, "Storm 2",
                    "Run the native version of sub_825DD778 (bone matrices and their inverses, "
                    "main thread) instead of its recompiled code (same results; read at every "
                    "call, so a diagnostic A/B can switch it)");
REXCVAR_DEFINE_BOOL(storm2_native_inverse_check, false, "Storm 2",
                    "Diagnostic: run sub_825DD778 recompiled and native from the same state and "
                    "log how many bytes of their output blocks and registers differ");
REXCVAR_DEFINE_INT32(storm2_native_inverse_stats, 0, "Storm 2",
                     "Log calls and time per call of sub_825DD778 every N seconds (0 = off)");

#include "sub_825DD778.inc"

namespace {

using Clock = std::chrono::steady_clock;

struct ModeStats {
  std::atomic<uint64_t> calls{0}, matrices{0}, ns{0};
};
ModeStats g_stats[2];  // [0] recompiled, [1] native
std::atomic<uint64_t> g_checked{0}, g_checked_bytes{0}, g_differing_calls{0},
    g_differing_bytes{0}, g_differing_regs{0}, g_differing_volatile{0};
std::atomic<int64_t> g_last_log_ns{0};

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
      .count();
}

void MaybeLog() {
  const int32_t interval = REXCVAR_GET(storm2_native_inverse_stats);
  if (interval <= 0 && !REXCVAR_GET(storm2_native_inverse_check)) {
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
    const uint64_t matrices = g_stats[mode].matrices.exchange(0);
    const uint64_t ns = g_stats[mode].ns.exchange(0);
    if (calls) {
      REXLOG_INFO("Storm 2 bone inverses sub_825DD778 ({}): {} calls, {} matrices in {:.1f} s; "
                  "{:.1f} ms per s, {:.1f} us per call, {:.2f} us per matrix",
                  mode ? "native" : "recompiled", calls, matrices, seconds,
                  double(ns) / 1e6 / seconds, double(ns) / 1e3 / double(calls),
                  matrices ? double(ns) / 1e3 / double(matrices) : 0.0);
    }
  }
  if (const uint64_t checked = g_checked.exchange(0)) {
    REXLOG_INFO("Storm 2 bone inverses check: {} calls compared ({} output bytes), {} differed: "
                "{} bytes, {} registers ({} of them r0/r5-r11, volatile)",
                checked, g_checked_bytes.exchange(0), g_differing_calls.exchange(0),
                g_differing_bytes.exchange(0), g_differing_regs.exchange(0),
                g_differing_volatile.exchange(0));
  }
}

void CopyOut(uint8_t* base, uint32_t address, uint32_t size, std::vector<uint8_t>& out) {
  out.resize(size);
  for (uint32_t done = 0; done < size;) {
    const uint32_t a = address + done;
    const uint32_t chunk = std::min(size - done, 0x1000u - (a & 0xFFFu));
    std::memcpy(&out[done], storm2_native::Host(base, a), chunk);
    done += chunk;
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

REX_EXTERN(__imp__sub_825DD778);

REX_HOOK_RAW(sub_825DD778) {
  const bool native = REXCVAR_GET(storm2_native_inverse);
  const bool stats = REXCVAR_GET(storm2_native_inverse_stats) > 0;
  const uint32_t object = ctx.r3.u32;
  const uint32_t matrices = ctx.r6.u32 - ctx.r5.u32 + 1;

  if (REXCVAR_GET(storm2_native_inverse_check) && matrices <= 4096) {
    // Each version allocates its own output block: compare the blocks'
    // contents, then point the object back at the recompiled code's block.
    const PPCContext entry = ctx;
    __imp__sub_825DD778(ctx, base);
    const PPCContext original_ctx = ctx;
    const uint32_t original_copies = storm2_native::Load32(base, object + 20);
    const uint32_t original_inverses = storm2_native::Load32(base, object + 24);
    std::vector<uint8_t> after_original, after_native;
    CopyOut(base, original_copies, matrices * 128, after_original);
    PPCContext native_ctx = entry;
    Native_sub_825DD778(native_ctx, base);
    CopyOut(base, storm2_native::Load32(base, object + 20), matrices * 128, after_native);
    storm2_native::Store32(base, object + 20, original_copies);
    storm2_native::Store32(base, object + 24, original_inverses);
    ctx = original_ctx;
    uint64_t differing = 0;
    for (size_t i = 0; i < after_original.size(); ++i) {
      differing += after_original[i] != after_native[i];
    }
    uint64_t regs = 0, volatile_regs = 0;
    for (int n = 0; n < 32; ++n) {
      if (Gpr(original_ctx, n) != Gpr(native_ctx, n)) {
        ++regs;
        volatile_regs += n == 0 || (n >= 5 && n <= 11);
      }
    }
    for (int i = 0; i < 32; ++i) {
      regs += std::memcmp(&original_ctx.f0 + i, &native_ctx.f0 + i, sizeof(PPCRegister)) != 0;
    }
    for (int i = 0; i < 128; ++i) {
      regs += std::memcmp(&original_ctx.v0 + i, &native_ctx.v0 + i, sizeof(PPCVRegister)) != 0;
    }
    g_checked.fetch_add(1, std::memory_order_relaxed);
    g_checked_bytes.fetch_add(after_original.size(), std::memory_order_relaxed);
    if (differing || regs) {
      if (g_differing_calls.fetch_add(1, std::memory_order_relaxed) < 2) {
        REXLOG_WARN("Storm 2 bone inverses check: r3={:08X} r4={:08X} r5={} r6={}: {} of {} "
                    "bytes and {} registers differ",
                    entry.r3.u32, entry.r4.u32, entry.r5.u32, entry.r6.u32, differing,
                    after_original.size(), regs);
      }
      g_differing_bytes.fetch_add(differing, std::memory_order_relaxed);
      g_differing_regs.fetch_add(regs, std::memory_order_relaxed);
      g_differing_volatile.fetch_add(volatile_regs, std::memory_order_relaxed);
    }
    MaybeLog();
    return;
  }

  const int64_t start = stats ? NowNs() : 0;
  if (native) {
    Native_sub_825DD778(ctx, base);
  } else {
    __imp__sub_825DD778(ctx, base);
  }
  if (stats) {
    ModeStats& s = g_stats[native ? 1 : 0];
    s.calls.fetch_add(1, std::memory_order_relaxed);
    s.matrices.fetch_add(matrices, std::memory_order_relaxed);
    s.ns.fetch_add(uint64_t(NowNs() - start), std::memory_order_relaxed);
    MaybeLog();
  }
}

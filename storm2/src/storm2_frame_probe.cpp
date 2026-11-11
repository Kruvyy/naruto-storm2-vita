// Storm 2: per-frame wall and CPU time of the main loop's parts
// (storm2_frame_probe). See storm2_probe.h.

#include "storm2_probe.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/perf/guest_frame_stats.h>
#include <rex/platform.h>

#if REX_PLATFORM_VITA
#include <psp2/kernel/threadmgr.h>
#endif

REXCVAR_DEFINE_INT32(storm2_frame_probe, 0, "Storm 2",
                     "Seconds between log lines with the wall and CPU time per frame of the "
                     "main loop's parts (frame sync, counter wait, game update...; 0 = off)")
    .range(0, 3600);

namespace storm2_probe {
namespace {

const char* const kNames[kCount] = {"frame loop", "frame sync", "counter wait", "job wait",
                                    "update",     "update A",   "update A1",    "update B",
                                    "update C",   "update C1"};

struct Totals {
  std::atomic<uint64_t> calls{0}, wall{0}, cpu{0};
};
Totals g_main[kCount];
Totals g_other[kCount];
std::atomic<uint64_t> g_main_thread{0};  // set by the first frame loop
std::atomic<uint64_t> g_last_report_us{0};
std::atomic<uint32_t> g_last_frame{0};

uint64_t ThisThread() {
#if REX_PLATFORM_VITA
  return uint64_t(uint32_t(sceKernelGetThreadId()));
#else
  return std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
}

void MaybeReport() {
  const int32_t seconds = REXCVAR_GET(storm2_frame_probe);
  const uint64_t now = WallUs();
  uint64_t last = g_last_report_us.load(std::memory_order_relaxed);
  if (!last) {
    if (g_last_report_us.compare_exchange_strong(last, now)) {
      g_last_frame.store(rex::perf::CurrentGuestFrame(), std::memory_order_relaxed);
    }
    return;
  }
  if (now - last < uint64_t(seconds) * 1000000 ||
      !g_last_report_us.compare_exchange_strong(last, now)) {
    return;
  }
  const uint32_t frame = rex::perf::CurrentGuestFrame();
  const uint32_t frames = std::max<uint32_t>(1, frame - g_last_frame.exchange(frame));
  const double secs = double(now - last) / 1e6;
  std::string text = fmt::format("Storm 2 frame probe: {} frames in {:.1f} s ({:.2f} frames/s); "
                                 "ms per frame, main thread [calls, wall, cpu]:",
                                 frames, secs, double(frames) / secs);
  for (int i = 0; i < kCount; ++i) {
    const uint64_t calls = g_main[i].calls.exchange(0);
    const uint64_t wall = g_main[i].wall.exchange(0);
    const uint64_t cpu = g_main[i].cpu.exchange(0);
    if (calls) {
      text += fmt::format(" {} [{:.1f}, {:.1f}, {:.1f}]", kNames[i], double(calls) / frames,
                          double(wall) / 1000.0 / frames, double(cpu) / 1000.0 / frames);
    }
  }
  text += "; other threads:";
  for (int i = 0; i < kCount; ++i) {
    const uint64_t calls = g_other[i].calls.exchange(0);
    const uint64_t wall = g_other[i].wall.exchange(0);
    const uint64_t cpu = g_other[i].cpu.exchange(0);
    if (calls) {
      text += fmt::format(" {} [{:.1f}, {:.1f}, {:.1f}]", kNames[i], double(calls) / frames,
                          double(wall) / 1000.0 / frames, double(cpu) / 1000.0 / frames);
    }
  }
  REXLOG_INFO("{}", text);
}

}  // namespace

bool On() { return REXCVAR_GET(storm2_frame_probe) > 0; }

uint64_t WallUs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

uint64_t ThreadCpuUs() {
#if REX_PLATFORM_VITA
  SceKernelThreadInfo info{};
  info.size = sizeof(info);
  if (sceKernelGetThreadInfo(0, &info) < 0) {
    return 0;
  }
  return uint64_t(info.runClocks);
#else
  return 0;
#endif
}

void Record(Id id, uint64_t wall_us, uint64_t cpu_us) {
  const uint64_t self = ThisThread();
  if (id == kUpdate) {
    // The game's update runs on the main thread (the frame loop function
    // itself never returns: it is the loop).
    uint64_t expected = 0;
    g_main_thread.compare_exchange_strong(expected, self);
  }
  Totals& t = (self == g_main_thread.load(std::memory_order_relaxed)) ? g_main[id] : g_other[id];
  t.calls.fetch_add(1, std::memory_order_relaxed);
  t.wall.fetch_add(wall_us, std::memory_order_relaxed);
  t.cpu.fetch_add(cpu_us, std::memory_order_relaxed);
  if (id == kUpdate) {
    MaybeReport();
  }
}

}  // namespace storm2_probe

// Hooks: each times the original function.
#define STORM2_PROBE_HOOK(fn, id)        \
  REX_EXTERN(__imp__##fn);               \
  REX_HOOK_RAW(fn) {                     \
    storm2_probe::Scope scope(storm2_probe::id); \
    __imp__##fn(ctx, base);              \
  }

STORM2_PROBE_HOOK(sub_825D03F0, kFrameSync)
STORM2_PROBE_HOOK(sub_825D6630, kJobWait)
STORM2_PROBE_HOOK(sub_822344F8, kUpdate)
STORM2_PROBE_HOOK(sub_82551BE8, kUpdateA)
STORM2_PROBE_HOOK(sub_8262F350, kUpdateA1)
STORM2_PROBE_HOOK(sub_82462AC8, kUpdateB)
STORM2_PROBE_HOOK(sub_825514F8, kUpdateC)
STORM2_PROBE_HOOK(sub_82635D30, kUpdateC1)

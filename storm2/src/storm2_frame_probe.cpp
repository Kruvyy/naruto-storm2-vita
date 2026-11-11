// Storm 2: per-frame wall and CPU time of the main loop's parts
// (storm2_frame_probe). See storm2_probe.h.

#include "storm2_probe.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/perf/guest_frame_stats.h>
#include <rex/platform.h>
#include <rex/system/xmemory.h>

#if REX_PLATFORM_VITA
#include <psp2/kernel/threadmgr.h>
#endif

// The task manager (r3) keeps its tasks in a list: head at +288, sentinel at
// +304; each node: +256 priority, +264 state (1 = run), +268 task object
// (vtable slot 1 is its update), +272 next, +280 the time the game measured
// for the task's last update (u64, its own timer). Summed by update function.
namespace storm2_probe {
struct TaskTime {
  uint64_t ticks = 0;
  uint64_t runs = 0;
  uint32_t priority = 0;
};
std::mutex g_task_mutex;
std::unordered_map<uint32_t, TaskTime> g_task_times;
uint64_t g_task_ticks_total = 0;

uint32_t Be32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, rex::memory::GuestPtr<const uint8_t*>(base, address), 4);
  return __builtin_bswap32(v);
}

void CollectTaskTimes(uint8_t* base, uint32_t manager, uint32_t lo, uint32_t hi) {
  const uint32_t sentinel = manager + 304;
  uint32_t node = Be32(base, manager + 288);
  std::lock_guard<std::mutex> lock(g_task_mutex);
  for (int guard = 0; node && node != sentinel && guard < 4096; ++guard) {
    const uint32_t priority = Be32(base, node + 256);
    if (priority >= lo && priority <= hi && Be32(base, node + 264) == 1) {
      const uint32_t task = Be32(base, node + 268);
      const uint32_t vtable = task ? Be32(base, task) : 0;
      uint32_t fn = vtable ? Be32(base, vtable + 4) : 0;
      if (fn == 0x82237A28 && task) {
        // A delegate task: +4 the object, +8 the function it calls.
        fn = Be32(base, task + 8);
      }
      const uint64_t ticks = (uint64_t(Be32(base, node + 280)) << 32) | Be32(base, node + 284);
      if (ticks < (uint64_t(1) << 40)) {
        TaskTime& t = g_task_times[fn];
        t.ticks += ticks;
        ++t.runs;
        t.priority = priority;
        g_task_ticks_total += ticks;
      }
    }
    node = Be32(base, node + 272);
  }
}
}  // namespace storm2_probe

namespace storm2_native_crt {
extern std::atomic<uint64_t> g_memcpy_calls, g_memcpy_bytes;
}

REXCVAR_DEFINE_INT32(storm2_frame_probe, 0, "Storm 2",
                     "Seconds between log lines with the wall and CPU time per frame of the "
                     "main loop's parts (frame sync, counter wait, game update...; 0 = off)")
    .range(0, 3600);

namespace storm2_probe {
extern std::atomic<uint32_t> g_update_r3[4];
extern std::atomic<uint64_t> g_update_wall[4], g_update_calls[4];
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
  const double update_wall_ms =
      double(g_main[kUpdate].wall.load() + g_other[kUpdate].wall.load()) / 1000.0 / frames;
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
  text += fmt::format("; memcpy {:.0f} calls {:.1f} KiB per frame",
                      double(storm2_native_crt::g_memcpy_calls.exchange(0)) / frames,
                      double(storm2_native_crt::g_memcpy_bytes.exchange(0)) / 1024.0 / frames);
  text += "; update by r4^(r5<<16):";
  for (int i = 0; i < 4; ++i) {
    const uint32_t r3 = g_update_r3[i].load();
    const uint64_t calls = g_update_calls[i].exchange(0), wall = g_update_wall[i].exchange(0);
    if (r3 && calls) {
      text += fmt::format(" {:08X} [{:.1f}, {:.1f} ms]", r3, double(calls) / frames,
                          double(wall) / 1000.0 / frames);
    }
  }
  REXLOG_INFO("{}", text);
  {
    // Tasks by update function: share of the time the game measured, scaled
    // to the update's measured wall time per frame.
    std::vector<std::pair<uint32_t, TaskTime>> tasks;
    uint64_t ticks_total;
    {
      std::lock_guard<std::mutex> lock(g_task_mutex);
      tasks.assign(g_task_times.begin(), g_task_times.end());
      ticks_total = g_task_ticks_total;
      g_task_times.clear();
      g_task_ticks_total = 0;
    }
    std::sort(tasks.begin(), tasks.end(),
              [](const auto& a, const auto& b) { return a.second.ticks > b.second.ticks; });
    std::string list = fmt::format("Storm 2 tasks ({} kinds; update wall {:.1f} ms/frame):",
                                   tasks.size(), update_wall_ms);
    for (size_t i = 0; i < tasks.size() && i < 25 && ticks_total; ++i) {
      const double share = double(tasks[i].second.ticks) / double(ticks_total);
      list += fmt::format(" {:08X} p{} {:.1f}x {:.1f}%={:.1f}ms;", tasks[i].first,
                          tasks[i].second.priority, double(tasks[i].second.runs) / frames,
                          share * 100.0, share * update_wall_ms);
    }
    REXLOG_INFO("{}", list);
  }
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

// sub_822344F8 runs 3 times a frame: its time kept apart by its r3 (up to 4).
namespace storm2_probe {
std::atomic<uint32_t> g_update_r3[4];
std::atomic<uint64_t> g_update_wall[4], g_update_calls[4];
}  // namespace storm2_probe


REX_EXTERN(__imp__sub_822344F8);
REX_HOOK_RAW(sub_822344F8) {
  const uint32_t manager = ctx.r3.u32, lo = ctx.r4.u32, hi = ctx.r5.u32;
  const uint32_t r3 = ctx.r4.u32 ^ (ctx.r5.u32 << 16);  // keyed by r4/r5 (r3 is the same)
  const uint64_t wall0 = storm2_probe::On() ? storm2_probe::WallUs() : 0;
  {
    storm2_probe::Scope scope(storm2_probe::kUpdate);
    __imp__sub_822344F8(ctx, base);
  }
  if (wall0) {
    storm2_probe::CollectTaskTimes(base, manager, lo, hi);
  }
  if (wall0) {
    const uint64_t wall = storm2_probe::WallUs() - wall0;
    for (int i = 0; i < 4; ++i) {
      uint32_t expected = 0;
      if (storm2_probe::g_update_r3[i].load() == r3 ||
          storm2_probe::g_update_r3[i].compare_exchange_strong(expected, r3)) {
        storm2_probe::g_update_wall[i].fetch_add(wall);
        storm2_probe::g_update_calls[i].fetch_add(1);
        break;
      }
    }
  }
}

// Hooks: each times the original function.
#define STORM2_PROBE_HOOK(fn, id)        \
  REX_EXTERN(__imp__##fn);               \
  REX_HOOK_RAW(fn) {                     \
    storm2_probe::Scope scope(storm2_probe::id); \
    __imp__##fn(ctx, base);              \
  }

STORM2_PROBE_HOOK(sub_825D03F0, kFrameSync)
// sub_825D6630 (kJobWait): timed in storm2_job_wait.cpp, which hooks it.
STORM2_PROBE_HOOK(sub_82551BE8, kUpdateA)
STORM2_PROBE_HOOK(sub_8262F350, kUpdateA1)
STORM2_PROBE_HOOK(sub_82462AC8, kUpdateB)
STORM2_PROBE_HOOK(sub_825514F8, kUpdateC)
STORM2_PROBE_HOOK(sub_82635D30, kUpdateC1)

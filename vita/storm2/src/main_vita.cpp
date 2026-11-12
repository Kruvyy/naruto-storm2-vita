// Storm 2 on the PS Vita, headless (B4 of docs/PLANO_RUNTIME_VITA.md).
//
// The ReXGlue runtime with the software MMU, the null GPU (the PM4 command
// stream runs, nothing is drawn), real XMA decoding with a silent real-time
// audio driver, and the built-in controls. Nothing is shown on screen apart
// from a status colour: yellow while starting, blue while the game runs, red
// on an error, green when the game ended.
//
// Files (all in ux0:data/storm2port):
//   game/            the game: default.xex and data/ (copied by the user)
//   storm2.toml      optional cvars (soft_mmu_caps, input_replay, log_level...)
//   storm2.log       runtime log
//   storm2_stats.log every 5 s: guest frames, frames/s, audio frames/s the
//                    game submits (real time = 187.5), free memory per pool,
//                    guest memory backed by host blocks, newlib heap in use
//   storm2_threads.log every 30 s: CPU time per thread (guest and runtime)
//   frame_stats.csv  per-frame statistics (perf_frame_stats; guest instruction
//                    counts are 0, the Vita build has no counters)
//
// Hold START + SELECT for 2 s to quit.

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <malloc.h>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <unwind-arm-common.h>  // the SDK has no unwind.h
#include <vector>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include <rex/audio/vita/vita_audio_system.h>
#include <rex/cvar.h>
#include <rex/graphics/gles/gles_gpu.h>
#include <rex/graphics/null/null_gpu.h>
#include <rex/image_info.h>
#include <rex/input/input_system.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/perf/guest_frame_stats.h>
#include <rex/ppc/intrinsics.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>
#include <spdlog/sinks/base_sink.h>

// Generated with the recompiled code (storm2_init.cpp).
extern const rex::PPCImageInfo PPCImageConfig;

#include <fast_tls.h>        // vita/runtime/compat
#include <heap_trace.h>      // vita/runtime/compat
#include <kernel_objects.h>  // vita/runtime/compat

// The real calls behind compat/kernel_objects.c's counting wrappers (--wrap).
extern "C" SceUID __real_sceKernelCreateSema(const char* name, SceUInt attr, int init, int max,
                                             SceKernelSemaOptParam* option);
extern "C" int __real_sceKernelDeleteSema(SceUID id);

namespace rex::thread {
void VitaPollWakeups(uint64_t out[3]);  // core/threading_vita.cpp
uint64_t VitaThreadBlockedUs(uint32_t system_id);  // core/threading_vita.cpp
void VitaReapplyThreadPlacement();  // core/threading_vita.cpp
size_t VitaLiveThreadObjects();     // core/threading_vita.cpp
bool VitaCore3Available();         // core/threading_vita.cpp
}
namespace rex::memory {
void VitaSetPoolBudgets(size_t main_bytes, size_t phycont_bytes, size_t cdram_bytes);
void VitaPoolUsage(size_t out[3]);  // core/memory_vita.cpp
void VitaBlockCounts(size_t* blocks, size_t* arenas);  // core/memory_vita.cpp
void VitaSetSmallBlocksFirstInCdram(size_t max_bytes);
}

// newlib's malloc heap is one block taken at start-up; the guest memory comes
// from memory blocks. 48 MiB ran out in battle in Vita3K (GLES vertex staging
// buffers, XMA decoders; bug 90); the target is Vita3K now (the game does not
// fit the console's memory, docs/bugs.md), where ~320 MiB are free at start.
// The console build (STORM2_CONSOLE) keeps it small: there the main RAM
// (~237 MiB free at start with ATTRIBUTE2=12) is what the game's memory needs.
extern "C" {
#if STORM2_CONSOLE
// 40 ran out on the title screen (a 3 MiB allocation, heap in pieces), before
// the XMA decoders were made smaller. With 42 a battle that kept running
// failed a 232 KiB allocation with 5.5 MiB free in pieces (Vita3K with the
// console's limits, ~frame 6650): 48 leaves room for that fragmentation.
unsigned int _newlib_heap_size_user = 48 * 1024 * 1024;
#else
unsigned int _newlib_heap_size_user = 96 * 1024 * 1024;
#endif
}

REXCVAR_DEFINE_STRING(vita_game_root, "ux0:data/storm2port/game", "Vita",
                      "Folder with default.xex and the game data");
REXCVAR_DEFINE_STRING(vita_gpu, "auto", "Vita",
                      "GPU backend: gles (renderer, needs ur0:data/external/libshacccg.suprx), "
                      "null (no rendering), auto (gles if the shader compiler is installed)");
REXCVAR_DEFINE_BOOL(vita_fast_tls, true, "Vita",
                    "Answer sceKernelGetTLSAddr (newlib, thread_local) from the TPIDRURO "
                    "register when a start-up probe confirms the layout (Vita3K emulates "
                    "every real call on the host)");
REXCVAR_DEFINE_INT32(vita_fast_tls_check, 0, "Vita",
                     "Compare every Nth fast TLS lookup with the real call and turn the fast "
                     "path off on a mismatch (0 = never)");
REXCVAR_DEFINE_INT32(vita_kernel_object_probe, 20000, "Vita",
                     "At start, before any other thread: make up to this many semaphores until "
                     "the kernel refuses, delete them, and write the count to storm2_stats.log "
                     "(the console's limit of kernel objects per process; 0 = off)")
    .range(0, 100000);
REXCVAR_DEFINE_STRING(vita_sample_thread, "", "Vita",
                      "Diagnostic: sample the link register of the guest thread whose name "
                      "contains this text (e.g. 'Main XThread') every ~1 ms and write the most "
                      "frequent call sites to storm2_samples.log every 30 s (empty = off)");
REXCVAR_DEFINE_INT32(vita_sample_top, 20, "Vita",
                     "Diagnostic: how many of the most frequent call chains vita_sample_thread "
                     "writes per thread and interval");
REXCVAR_DEFINE_INT32(vita_ab_seconds, 0, "Vita",
                     "Diagnostic: switch to the next state of vita_ab_cycle every N seconds "
                     "(0 = off); each switch is logged, so one run on one screen compares them");
REXCVAR_DEFINE_INT32(vita_ab_start_seconds, 0, "Vita",
                     "Diagnostic: seconds after the start before vita_ab_cycle begins (its first "
                     "state is applied then)");
REXCVAR_DEFINE_INT32(vita_ab_stop_seconds, 0, "Vita",
                     "Diagnostic: seconds after the start when vita_ab_cycle stops at its first "
                     "state for the rest of the run (0 = never)");
REXCVAR_DEFINE_STRING(vita_ab_cycle, "", "Vita",
                      "Diagnostic: states separated by '|', each 'cvar=value;cvar=value' on top "
                      "of the values at start (an empty state = those values). Only cvars read "
                      "while running, e.g. \"|storm2_job_wait_sleep_us=100\"");
REXCVAR_DEFINE_STRING(vita_memory_budgets_mib, "", "Vita",
                      "Diagnostic: 'MAIN,PHYCONT,CDRAM' MiB the runtime's memory blocks (the "
                      "game's memory) may take from each kind of memory; past one it is "
                      "skipped as if full. Vita3K never refuses memory: this makes a run "
                      "there meet the console's limits (empty = no budgets)");
REXCVAR_DEFINE_INT32(vita_memory_ballast_mib, 0, "Vita",
                     "Hold this many MiB of main RAM from the start (Vita3K: about 80 makes "
                     "its free memory match the console's; diagnostic)");
REXCVAR_DEFINE_INT32(vita_heap_trace, 0, "Vita",
                     "Record the call paths of newlib heap allocations of at least this many "
                     "bytes and write the biggest holders every 30 s to storm2_heap.log "
                     "(0 = off; diagnostic)");

namespace {
constexpr const char* kShaderCompilerPath = "ur0:data/external/libshacccg.suprx";
bool g_rendering = false;
}  // namespace

namespace {

constexpr const char* kDataDir = "ux0:data/storm2port";
constexpr const char* kStatsPath = "ux0:data/storm2port/storm2_stats.log";
constexpr const char* kFrameStatsPath = "ux0:data/storm2port/frame_stats.csv";
constexpr const char* kImportStatsPath = "ux0:data/storm2port/import_stats.csv";
constexpr const char* kHeapTracePath = "ux0:data/storm2port/storm2_heap.log";

void Stats(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Stats(const char* fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return;
  }
  SceUID fd = sceIoOpen(kStatsPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) {
    sceIoWrite(fd, line, std::min<size_t>(size_t(n), sizeof(line) - 1));
    sceIoClose(fd);
  }
}

// Critical log messages (REX_FATAL: a software MMU limit, a call to an invalid
// guest address...) also go to storm2_stats.log, opened and closed for each
// one: the program aborts right after them, and Vita3K writes storm2.log (kept
// open) only up to an earlier point, so the message itself was lost.
class StatsCriticalSink : public spdlog::sinks::base_sink<std::mutex> {
 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);
    SceUID fd = sceIoOpen(kStatsPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
      sceIoWrite(fd, formatted.data(), formatted.size());
      sceIoClose(fd);
    }
  }
  void flush_() override {}
};

uint32_t* g_fb = nullptr;
void Screen(uint32_t color) {
  if (!g_fb) {
    SceUID block = sceKernelAllocMemBlock("storm2_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                          2 * 1024 * 1024, nullptr);
    if (block < 0) {
      return;
    }
    void* base = nullptr;
    sceKernelGetMemBlockBase(block, &base);
    g_fb = static_cast<uint32_t*>(base);
    SceDisplayFrameBuf fb{};
    fb.size = sizeof(fb);
    fb.base = g_fb;
    fb.pitch = 960;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    fb.width = 960;
    fb.height = 544;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
  }
  for (int i = 0; i < 960 * 544; ++i) {
    g_fb[i] = color;
  }
}

constexpr uint32_t kYellow = 0xFF20E0E0u, kBlue = 0xFFE08020u, kRed = 0xFF2020E0u,
                   kGreen = 0xFF20C020u;

[[noreturn]] void Fail(const std::string& message) {
  REXLOG_ERROR("{}", message);
  Stats("ERROR: %s\n", message.c_str());
  rex::FlushLogging();
  Screen(kRed);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
  SceCtrlData pad{};
  for (;;) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
      break;
    }
    sceKernelDelayThread(50 * 1000);
  }
  sceKernelExitProcess(1);
  __builtin_unreachable();
}

// A failed host allocation (operator new below). Without this, std::bad_alloc
// reaches std::terminate and abort, and newlib's _exit frees the heap block
// before the process ends: every other thread then crashes on freed memory and
// the Vita3K log shows those crashes, not the allocation (bug 46).
struct Backtrace {
  uintptr_t pc[16];
  int count = 0;
};

_Unwind_Reason_Code BacktraceStep(_Unwind_Context* context, void* arg) {
  auto* bt = static_cast<Backtrace*>(arg);
  if (bt->count >= 16) {
    return _URC_END_OF_STACK;
  }
  bt->pc[bt->count++] = uintptr_t(_Unwind_GetGR(context, 15) & ~_Unwind_Word(1));  // pc
  return _URC_NO_REASON;
}

std::atomic<bool> g_out_of_memory{false};

[[noreturn]] void OutOfHostMemory(size_t size) {
  if (g_out_of_memory.exchange(true)) {
    // Another thread is already reporting; keep the heap and wait for it.
    for (;;) {
      sceKernelDelayThread(1000 * 1000);
    }
  }
  Backtrace bt;
  _Unwind_Backtrace(BacktraceStep, &bt);
  char trace[16 * 9 + 1] = {};
  for (int i = 0; i < bt.count; ++i) {
    snprintf(trace + i * 9, 10, " %08X", unsigned(bt.pc[i]));
  }
  vita_heap_trace_dump(kHeapTracePath, "allocation failed");
  const struct mallinfo heap = mallinfo();
  // Integers only: no heap use while reporting.
  Stats("ERROR: host allocation of %u bytes failed: newlib heap %u KiB in use, %u KiB free, "
        "arena %u of %u KiB; thread %08X; stack%s\n",
        unsigned(size), unsigned(heap.uordblks / 1024), unsigned(heap.fordblks / 1024),
        unsigned(heap.arena / 1024), _newlib_heap_size_user / 1024,
        unsigned(sceKernelGetThreadId()), trace);
  Fail(fmt::format("Host memory exhausted: {} bytes requested, newlib heap {} KiB in use, {} KiB "
                   "free (stack in storm2_stats.log)",
                   size, heap.uordblks / 1024, heap.fordblks / 1024));
}

}  // namespace

void* operator new(std::size_t size) {
  if (void* p = std::malloc(size ? size : 1)) {
    return p;
  }
  OutOfHostMemory(size);
}
void* operator new[](std::size_t size) {
  return operator new(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return std::malloc(size ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return std::malloc(size ? size : 1);
}

namespace {

// Guest call-site sampler (vita_sample_thread): a host thread reads the link
// register the generated code keeps in the guest thread's PPCContext (set at
// every call) about every millisecond; the most frequent values, written to
// storm2_samples.log every 30 s, show where that thread spends its time on a
// console, which has no profiler for applications. Map an address to its
// function with the generated function table (largest start below it).
constexpr const char* kSamplesPath = "ux0:data/storm2port/storm2_samples.log";
// vita_sample_thread names threads, comma-separated, or "*" for every guest
// thread (up to kMaxSampled). Each sample is the link register plus up to three
// return addresses read up the guest stack (back chain at 0(r1), the caller's
// LR saved at -8 of each frame), so threads that all wait in one helper are
// told apart by who called it.
constexpr size_t kMaxSampled = 32;
constexpr size_t kChain = 8;
using SampleKey = std::array<uint32_t, kChain>;
struct SampleKeyHash {
  size_t operator()(const SampleKey& k) const {
    size_t h = 0;
    for (uint32_t v : k) {
      h = h * 1000003u ^ v;
    }
    return h;
  }
};
struct SampledThread {
  std::atomic<PPCContext*> ctx{nullptr};
  uint32_t stack_low = 0;   // the stack walk reads only [stack_low, stack_high)
  uint32_t stack_high = 0;
  std::string name;
  std::unordered_map<SampleKey, uint32_t, SampleKeyHash> counts;
  uint64_t total = 0;
};
struct Sampler {
  std::mutex mutex;
  SampledThread threads[kMaxSampled];
  std::atomic<size_t> count{0};
  bool all = false;
  const rex::memory::Memory* memory = nullptr;
};
Sampler g_sampler;

// 0 for an unmapped page: the soft MMU's page table has a null entry there
// (a thread's stack page before it is touched), so the translated pointer is
// below 4 KiB; reading it faulted the sampler on a console.
// The page must also be committed: a thread that exits frees its guest stack
// (XThread::Exit) while the sampler may still walk it (that faulted too).
uint32_t SampleLoad32(uint32_t guest_address) {
  auto* memory = const_cast<rex::memory::Memory*>(g_sampler.memory);
  auto* heap = memory->LookupHeap(guest_address);
  uint32_t protect = 0;
  if (!heap || !heap->QueryProtect(guest_address, &protect) ||
      !(protect & rex::memory::kMemoryProtectRead)) {
    return 0;
  }
  const uint8_t* p = g_sampler.memory->TranslateVirtual<const uint8_t*>(guest_address);
  if (reinterpret_cast<uintptr_t>(p) < 0x10000) {
    return 0;
  }
  const uint32_t v = *reinterpret_cast<const volatile uint32_t*>(p);
  return __builtin_bswap32(v);
}

int SamplerThread(SceSize, void*) {
  for (;;) {
    sceKernelDelayThread(1000);
    const size_t count = g_sampler.count.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) {
      SampledThread& t = g_sampler.threads[i];
      PPCContext* ctx = t.ctx.load(std::memory_order_relaxed);
      if (!ctx) {
        continue;
      }
      SampleKey key{};
      key[0] = uint32_t(*reinterpret_cast<volatile uint64_t*>(&ctx->lr));
      uint32_t sp = uint32_t(*reinterpret_cast<volatile uint64_t*>(&ctx->r1.u64));
      // Only inside the thread's own stack: a thread that has not run yet has
      // r1 at the stack top, and the page above it is not backed (reading it
      // faulted on a console).
      auto in_stack = [&](uint32_t a) { return a >= t.stack_low && a + 4 <= t.stack_high; };
      for (size_t k = 1; k < kChain && g_sampler.memory; ++k) {
        if ((sp & 3) || !in_stack(sp)) {
          break;
        }
        const uint32_t next = SampleLoad32(sp);
        if (next <= sp || (next & 3) || !in_stack(next - 8)) {
          break;
        }
        key[k] = SampleLoad32(next - 8);
        sp = next;
      }
      std::lock_guard<std::mutex> lock(g_sampler.mutex);
      ++t.counts[key];
      ++t.total;
    }
  }
  return 0;
}

void WriteSamples(uint64_t now_us) {
  std::string text;
  const size_t count = g_sampler.count.load(std::memory_order_acquire);
  for (size_t n = 0; n < count; ++n) {
    SampledThread& t = g_sampler.threads[n];
    std::vector<std::pair<SampleKey, uint32_t>> top;
    uint64_t total;
    {
      std::lock_guard<std::mutex> lock(g_sampler.mutex);
      top.assign(t.counts.begin(), t.counts.end());
      total = t.total;
      t.counts.clear();
      t.total = 0;
    }
    if (!total) {
      continue;
    }
    std::sort(top.begin(), top.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    text += fmt::format("t={}s {} samples of the guest link register of '{}'\n",
                        now_us / 1000000, total, t.name);
    const size_t keep = g_sampler.all ? 6 : size_t(std::max(1, REXCVAR_GET(vita_sample_top)));
    for (size_t i = 0; i < top.size() && i < keep; ++i) {
      text += fmt::format("  {:5.1f}%  {:08X}", 100.0 * top[i].second / double(total),
                          top[i].first[0]);
      for (size_t k = 1; k < kChain; ++k) {
        text += fmt::format(" < {:08X}", top[i].first[k]);
      }
      text += fmt::format("  ({})\n", top[i].second);
    }
  }
  if (text.empty()) {
    return;
  }
  SceUID fd = sceIoOpen(kSamplesPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) {
    sceIoWrite(fd, text.data(), text.size());
    sceIoClose(fd);
  }
}

// CPU time per thread: guest threads and the runtime's host threads (XMA
// decoder, GPU command processor...), from the kernel's run time of each
// thread (SceKernelThreadInfo::runClocks, microseconds [H]).
constexpr const char* kThreadsPath = "ux0:data/storm2port/storm2_threads.log";

void ThreadReport(rex::Runtime* runtime, uint64_t now_us, uint64_t interval_us,
                  std::map<SceUID, uint64_t>& last_run) {
  struct Row {
    std::string name;
    uint32_t guest_id;
    double share;
    int core;
    int priority;
    uint64_t run_us;
    int guest_cpu;      // guest hardware thread (0-5) the game asked for
    uint64_t mask;      // affinity mask in guest hardware threads (two per Vita core)
    uint32_t stack_used_kib = 0, stack_kib = 0;  // vita_thread_stack_paint
  };
  static const bool stack_paint = rex::cvar::GetFlagByName("vita_thread_stack_paint") == "true";
  std::vector<Row> rows;
  std::map<SceUID, uint64_t> run;
  bool no_kernel_run_time = false;
  auto threads =
      runtime->kernel_state()->object_table()->GetObjectsByType<rex::system::XThread>();
  for (auto& xthread : threads) {
    rex::thread::Thread* thread = xthread->thread();
    if (!thread) {
      continue;
    }
    const SceUID uid = SceUID(thread->system_id());
    SceKernelThreadInfo info{};
    info.size = sizeof(info);
    if (sceKernelGetThreadInfo(uid, &info) < 0) {
      continue;
    }
    uint64_t run_us = info.runClocks;
    if (run_us == 0) {
      // Vita3K reports no run time: time not blocked in a wait or sleep instead
      // (the thread ran, or wanted to run); only its change between reports
      // is used.
      run_us = now_us - std::min(now_us, rex::thread::VitaThreadBlockedUs(uint32_t(uid)));
      no_kernel_run_time = true;
    }
    run[uid] = run_us;
    auto it = last_run.find(uid);
    const uint64_t delta = it != last_run.end() && run_us >= it->second ? run_us - it->second : 0;
    // The thread's name carries the guest handle (F8000068...), as in the PC's
    // thread_insns.csv ("GPU Commands (F8000014)").
    rows.push_back({thread->name().empty()
                        ? fmt::format("{} ({:08X})", info.name, uint32_t(xthread->handle()))
                        : thread->name(),
                    xthread->thread_id(), double(delta) / double(interval_us), info.lastExecutedCpuId,
                    info.currentPriority, run_us, int(xthread->active_cpu()),
                    thread->affinity_mask()});
    if (stack_paint && info.stack && info.stackSize) {
      // Deepest use: from the bottom, the first word no longer the pattern
      // that vita_thread_stack_paint wrote at the thread's start.
      const uint32_t* p = static_cast<const uint32_t*>(info.stack);
      const uint32_t* end = p + info.stackSize / 4;
      while (p < end && *p == 0xA5C3A5C3u) {
        ++p;
      }
      const size_t used = size_t(reinterpret_cast<const uint8_t*>(end) -
                                 reinterpret_cast<const uint8_t*>(p));
      rows.back().stack_used_kib = uint32_t(used / 1024);
      rows.back().stack_kib = uint32_t(info.stackSize / 1024);
    }
  }
  last_run = std::move(run);
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.share > b.share; });
  double total = 0;
  uint64_t total_run_us = 0;
  for (const Row& row : rows) {
    total += row.share;
    total_run_us += row.run_us;
  }
  std::string text =
      fmt::format("t={}s threads={} total={:.0f}% of one core (run time since start {:.1f}s)\n",
                  now_us / 1000000, rows.size(), total * 100, double(total_run_us) / 1e6);
  // Vita3K reports no run time: then every thread is listed, for the guest
  // hardware thread and the affinity mask (which Vita core it is held to),
  // with its share of time not blocked (ran or waited for a core).
  const bool no_run_time = no_kernel_run_time;
  if (no_run_time) {
    text += "  (Vita3K: share = time not blocked in a wait or sleep)\n";
  }
  for (const Row& row : rows) {
    if (row.share < 0.005 && !no_run_time) {
      continue;
    }
    text += fmt::format("  {:5.1f}%  core {}  prio {:3}  hw {} mask {:02X}  {:08X}  {}{}\n",
                        row.share * 100, row.core, row.priority, row.guest_cpu,
                        unsigned(row.mask), row.guest_id, row.name,
                        row.stack_kib ? fmt::format("  stack {}/{} KiB", row.stack_used_kib,
                                                    row.stack_kib)
                                      : std::string());
  }
  SceUID fd = sceIoOpen(kThreadsPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) {
    sceIoWrite(fd, text.data(), text.size());
    sceIoClose(fd);
  }
}

void LogMemory(const char* when) {
  SceKernelFreeMemorySizeInfo mem{};
  mem.size = sizeof(mem);
  if (sceKernelGetFreeMemorySize(&mem) >= 0) {
    Stats("%s: free user %d KiB, cdram %d KiB, phycont %d KiB\n", when, mem.size_user / 1024,
          mem.size_cdram / 1024, mem.size_phycont / 1024);
  }
}

}  // namespace

int main() {
  sceIoMkdir(kDataDir, 0777);
  sceIoRemove(kStatsPath);
  sceIoRemove(kThreadsPath);
  sceIoRemove(kFrameStatsPath);
  sceIoRemove(kImportStatsPath);
  Screen(kYellow);

  // cvars: the optional storm2.toml, then the Vita defaults below for every
  // cvar the file does not set (SetFlagByName outranks the file, so setting
  // them first would ignore the file's values).
  char arg0[] = "storm2";
  char* argv[] = {arg0, nullptr};
  rex::cvar::Init(1, argv);
  rex::cvar::LoadConfig(std::string(kDataDir) + "/storm2.toml");
  auto app_default = [](const char* name, const char* value) {
    if (rex::cvar::GetFlagSource(name) == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName(name, value);
    }
  };
  app_default("perf_frame_stats", "true");
  app_default("perf_frame_stats_file", kFrameStatsPath);
  app_default("perf_import_stats_file", kImportStatsPath);
#if STORM2_CONSOLE
  // Console build: the game's memory gets the main RAM. These come first (the
  // first app_default for a cvar wins) and are a starting point, measured with
  // vita_memory_ballast_mib in Vita3K (docs/bugs.md, bug 94).
  // Game data with textures at a quarter of their size (tools/xfbin run twice;
  // changed files stored uncompressed, which the game then does not inflate).
  app_default("vita_game_root", "ux0:data/storm2port/game_quarter");
  // Passes whose result is not used: the motion blur's velocity pass (PS
  // A4A965 with its vertex shaders: it draws the whole scene again) and the
  // camera velocity pass (A2147ED6); the motion blur output is the plain
  // scene (gles_ps_output_override).
  // Light mode: two full-screen passes (one draw a frame each) whose absence
  // left the mode select unchanged in Vita3K; the console's frame rate rose
  // 16% and 5% with each skipped (gles_profile_ps_seconds). The other costly
  // pixel shaders draw the menus' own frames and text (skipping them broke
  // the title screen).
  // The whole velocity pass (A4A965C1 with every vertex shader) and the motion
  // blur pass BCC46076 (which gles_ps_output_override turns into a copy of the
  // scene into the target that already holds it): the console's Press Start
  // went from ~11.2 to ~14.8 frames/s (A/B in one run), Vita3K captures equal.
  // With only BCC46076 skipped the battle showed the velocity buffer (a bluish
  // band) that the copy used to cover.
  app_default("gles_skip_ps", "A4A965C1,A2147ED6,6D868915,66E05C9B,BCC46076");
  // A resolve that repeats the last copy into its target from an unchanged
  // source is not made again (Press Start: 7 of ~35 copies a frame, among them
  // the second color target, only written through MRT, copied 6 times;
  // Vita3K captures equal).
  app_default("gles_skip_repeated_resolves", "true");
  // Real memory only for the ranges of the big guest regions the game uses
  // (MiB, from the usage maps of soft_mmu_report_interval, with 1 MiB of
  // margin). A write elsewhere stops the game with a message naming the region.
  // These cover the title, the menus, the character select and a battle
  // (~265 MiB of blocks instead of 302): the character select loads the
  // fighters into 0xAB000000 up to MiB 162 (with 76-143 the console stopped
  // there, "wrote past the limit of physical region AB000000"), the battle
  // writes 0x40F40000 up to MiB 38 and 0xDC000000 up to 30 (Vita3K usage maps
  // with 1 MiB of margin; docs/bugs.md, bug 94). vsize:16: a 16 MiB virtual
  // region whose address changes from run to run, of which only the first MiB
  // is written. All of it is backed from the start. 0xAB000000 2-34 whole: the
  // game fills chunks starting every 8 MiB there (MiB 3, 11, 19, 27 in the
  // usage maps) a little more as a battle goes on (soft_mmu_guard_per_mib in
  // Vita3K: MiB 23 written past frame 7800 with 18-23, then MiB 8 past frame
  // 8200 with 2-8); the other ranges end 1 MiB past the usage maps.
  app_default("soft_mmu_caps",
              "0xAB000000=2-34|42-47|66-70|76-167,"
              "0x40F40000=0-40|67-71,0xDC000000=0-12|14-32|38-46,0xC0000000=0-1,"
              "vsize:16=0-2");
  // The guest's physical memory from the physically contiguous RAM and CDRAM
  // first: the main RAM is kept for the virtual heaps' big blocks (0x40F40000
  // needs 40 MiB in one piece, which found no room after the physical regions).
  app_default("soft_mmu_physical_other_pools", "true");
  // vitaGL pools: CDRAM for textures and render targets, a small RAM pool for
  // the vertex stream ring (1 MiB segments, two copies: 8 MiB; what does not
  // fit gets its own buffers); most of the physically contiguous RAM is left
  // to the game's memory.
  // Render, resolve and depth targets at 56.25%: 720x405 (the user's choice;
  // their ~42 MiB at full size filled a 56 MiB pool, and the GPU draws less).
  app_default("gles_render_scale_percent", "56.25");
  // 480x272 (the user's choice for speed): the menu's GPU time on the console
  // scaled with the pixel count (480x272 instead of 720x408: -55%), while the
  // game logic alone caps the menu near 6.7 frames/s.
  app_default("gles_render_size", "480x272");
  // Half precision pixel shaders: on the console the menu's GPU time is the
  // pixel shaders' (~296 ms a frame with them, ~7 ms with solid-color ones).
  app_default("gles_ps_precision", "mediump");
  // Post-processing targets (color without depth) at half the render scale:
  // ~80% of the menu's GPU time on the console went to their scenes.
  app_default("gles_vita_post_target_divisor", "2");
  // Plain products in every pixel shader except the characters' toon shader:
  // the menu's GPU time on the console went from ~278 to ~148 ms a frame.
  app_default("gles_mul_plain_filter", "*");
  app_default("gles_mul_zero_rule_filter", "B958D20D");
  // Texture fetches without the swizzle matrix when it is the identity.
  app_default("gles_identity_swizzle_variants", "true");
  // Core 0 left to the game's main thread (vita_thread_cores names it there):
  // in the menu it is the limit, and the audio thread ran on its core.
  app_default("vita_reserved_core", "0");
  // 60 MiB: the battle ran out of a 48 MiB pool while it loaded (Vita3K with
  // the console's limits: VRAM 4 MiB free, uploads waiting for vitaGL's
  // garbage collector); with 72 it used up to ~46 MiB. The console's menus
  // left 46 MiB of main RAM free and 4 MiB of CDRAM: the game's blocks now
  // take less CDRAM (budgets below) and the pool gets it. 52: a battle run
  // with 60 left at least 16.3 MiB of it free.
  app_default("gles_vita_cdram_pool_mib", "52");
  // Depth-only passes draw into a framebuffer whose depth buffer vitaGL sizes
  // like its color attachment: a 16x16 scratch color left a 16x16 depth buffer
  // that the console's GPU wrote far past (heap corruption, a crash in free()
  // as the loading screen ended; Vita3K does not show it). Same size as the
  // depth target instead.
  app_default("gles_depth_only_target_size", "0");
  // A smaller arena for the runtime shader compiler (~20 MiB of main RAM are
  // left on the menus); shaders are compiled on the device only when missing
  // from the shader cache.
  app_default("gles_vita_compiler_arena_mib", "12");
  // vitaGL keeps ~8 MiB of its own in the RAM pool (with a 6 MiB per-frame
  // pool); with 12 the ring did not fit and went to the CDRAM pool. The ring:
  // 2 MiB segments (two copies, 3 segments: 12 MiB; vitaGL holds ~10.6 MiB
  // of the RAM pool before it, so 24). With 1.5 MiB the battle (up to ~6.2
  // MiB of new vertices in a frame while it loads, up to ~2.5 once it runs)
  // put the rest in buffers of their own (12599 in 300 frames), which filled
  // the VRAM pool of 48 MiB. 3 segments instead of 4: with two display buffers
  // the GPU is at most two swaps behind, and vitaGL itself frees replaced
  // buffers 3 swaps later.
  app_default("gles_vita_ram_pool_mib", "24");
  app_default("gles_vita_circular_pool_mib", "6");
  app_default("gles_vita_phycont_pool_mib", "8");
  // The common dialog memory (8984 KiB, a budget of its own that the port's
  // dialog-free program leaves unused) as a fourth vitaGL pool: the battle
  // filled the other three while it loaded (Vita3K, 2026-11-12: VRAM 27.6 MiB
  // free in 52 with an 8 MiB largest block, RAM 1.4 of 24, phycont 7.9 of 8,
  // then a vertex buffer found no room and the GPU thread waited ~4 s per try).
  app_default("gles_vita_cdlg_pool_kib", "8984");
  app_default("gles_vertex_stream_mib", "1");
  app_default("gles_vertex_stream_kib", "2048");
  app_default("gles_vertex_stream_segments", "3");
  // sceGxm's parameter buffer (outside the pools, in CDRAM: vitaGL takes 82
  // MiB of the console's CDRAM with a 64 MiB pool) at 8 MiB instead of 16 for
  // 480x272 scenes, and two display buffers instead of three (2 MiB each of
  // the VRAM pool): memory the battle needs.
  app_default("gles_vita_param_buffer_mib", "8");
  app_default("gles_vita_display_buffers", "2");
  app_default("gles_vertex_stream_usage", "1");
  // Cached vertex/index buffers in the CDRAM pool, not the main RAM.
  app_default("gles_vertex_buffer_usage", "0");
  // The game's audio thread took a whole core on the loading screen, which
  // waited for it: the reverb alone (native) used 88% of a core at 500 MHz.
  // The sound output is discarded on the Vita for now, so it is not run.
  app_default("storm2_skip_reverb", "true");
  // The software MMU's usage report scans all backed guest memory for non-zero
  // pages: 0.7-1.05 s on the console's main thread (inside VdSwap) every 600
  // frames, a freeze every ~1.5 min on the menus. The per-frame CSV is written
  // to the memory card by the main thread too. Both are diagnostics.
  app_default("soft_mmu_report_interval", "0");
  app_default("perf_frame_stats", "false");
  // Host stacks of the game's threads: 128 KiB instead of 2 MiB (~18 threads,
  // ~34 MiB of main RAM). Measured with vita_thread_stack_paint through the
  // menus into a battle: the deepest guest thread used 16 KiB.
  app_default("guest_thread_host_stack_kib", "128");
  // The runtime's own threads (audio, XMA, vsync, dispatch) used at most 19
  // KiB: 256 KiB each instead of 2 MiB. The GPU command thread keeps 1 MiB: it
  // runs the shader compiler when a shader is missing from the cache.
  app_default("host_thread_host_stack_kib", "256");
  app_default("thread_host_stack_overrides", "GPU Commands=1024");
  // Diagnostics stay off (they cost CPU); storm2.toml turns them on, for
  // example vita_sample_thread = "*" and kernel_sync_stats = 10.
#endif
  // The physical pool at 0xAB000000 reserves 272 MiB and the game fills it from
  // the start; 184 MiB ran the title screen on the PC (docs/logs/E13d). A write
  // past the limit stops the game with a message naming the cvar.
  app_default("soft_mmu_caps", "0xAB000000=184");
  // Compiled shaders of the game, made on this device by the runtime compiler
  // on first use (the files derive from the game's shaders: never distribute).
  app_default("gles_shader_cache_dir", "ux0:data/storm2port/shader_cache");
  // The movies are decoded by the game's own code: ~0.2-0.3 frames/s in
  // Vita3K. Skipped by default (storm2_skip_movies = false in storm2.toml
  // plays them).
  app_default("storm2_skip_movies", "true");
  // Threads left on the default affinity share one core (Vita3K-Plus with
  // accurate scheduling runs them one at a time). The game's main thread, the
  // GPU command thread and the game's render thread (guest handle F8000068)
  // each get a core of their own; the rest stay on the default affinity.
  // Only one thread per core: two threads of the same priority on one core
  // can starve each other (docs/bugs.md, bug 79).
  app_default("vita_thread_cores", "Main XThread=0,GPU Commands=1,F8000068=2");
  // vitaGL pools: the first battle needs ~158 MiB of them (VRAM 100 + RAM 24 +
  // phycont 16 = 140 ran out: the texture upload wrote through a failed
  // allocation, bug 64). Vita3K does not limit memory; on the console the main
  // RAM is the scarce one (docs/RELATORIO_VIABILIDADE.md).
  // 128 MiB in Vita3K (no memory limit there; the console target was dropped):
  // the battle filled 64 with the vertex ring and own vertex buffers.
  app_default("gles_vita_ram_pool_mib", "128");
  // Vertex stream ring: busy battle frames (chakra attacks) want up to ~6.7
  // MiB. Created as GL_DYNAMIC_DRAW, vitaGL puts it in the RAM pool; as
  // GL_STATIC_DRAW it took 48 MiB of the 100 MiB VRAM pool, which then ran
  // empty in battle and the fighters vanished. (A 106 MiB VRAM pool stopped
  // the start-up: vitaGL's display buffers need the rest of the CDRAM.)
  app_default("gles_vertex_stream_mib", "6");
  app_default("gles_vertex_stream_usage", "1");
  // Same for the cached vertex/index buffers: as static they grew to ~14 MiB of
  // VRAM late in battle, which ran empty again (fighters vanished at frame ~9100).
  app_default("gles_vertex_buffer_usage", "1");
  // Depth of field blend (PS 478E5FA9 in the menu, 1C5B744F in battle): it
  // weighs the sharp scene (tf0) against the blurred one by the depth, which
  // the backend cannot copy (k_24_8 reads black), so the whole image came out
  // doubled and hazy, with a halo around the characters. The sharp scene is
  // used as is (no depth of field). The stale battle scene once blamed on this
  // was the vrsqrtefp NaN (bug 97). Bug 95.
  // The battle's motion blur (PS BCC46076: scene tf0 moved along the velocity
  // buffer) is then shown directly, and in a chakra dash its velocities held
  // garbage over part of the screen (stretched stale strips): it outputs the
  // scene unblurred too.
  app_default("gles_ps_output_override",
              "478E5FA9=texture2D(xe_s0, xe_i0.xy);1C5B744F=texture2D(xe_s0, xe_i0.xy);"
              "BCC46076=texture2D(xe_s0, xe_i0.xy)");
  // The vertex shaders of the motion blur velocity pass (PS A4A965): with the
  // Shader Model 3 multiplication their velocities came out as garbage in
  // Vita3K (characters smeared, colored shapes in battle; bug 89). Plain
  // multiplication gives the PC's result there.
  app_default("gles_mul_plain_filter", "0F628CC3,3FF1898E,FACBF5B5");

  {
    rex::LogConfig log_config;
    const std::string log_path = std::string(kDataDir) + "/storm2.log";
    sceIoRemove(log_path.c_str());
    log_config.log_file = log_path.c_str();
    auto critical_sink = std::make_shared<StatsCriticalSink>();
    critical_sink->set_level(spdlog::level::critical);
    log_config.extra_sinks.push_back(critical_sink);
    rex::InitLogging(log_config);
  }
  rex::cvar::FinalizeInit();
  // An uncaught C++ exception (on the console, a filesystem_error from a
  // "ux0:" path ended the program with only a core dump): write its message
  // to storm2_stats.log and the log before stopping.
  std::set_terminate([] {
    std::string what = "unknown exception";
    if (auto e = std::current_exception()) {
      try {
        std::rethrow_exception(e);
      } catch (const std::exception& ex) {
        what = ex.what();
      } catch (...) {
      }
    }
    Fail("Uncaught C++ exception: " + what);
  });
  if (REXCVAR_GET(vita_heap_trace) > 0) {
    sceIoRemove(kHeapTracePath);
    vita_heap_trace_enable(uint32_t(REXCVAR_GET(vita_heap_trace)));
  }
  LogMemory("start");
  // Core 3 (CapUnlocker) before any thread placement reads the core settings.
  rex::thread::VitaCore3Available();
  if (const int32_t probe = REXCVAR_GET(vita_kernel_object_probe); probe > 0) {
    // How many kernel objects the process may still make: the console refused
    // new semaphores (0x8002450D, SCE_KERNEL_ERROR_UID_RL_OVERFLOW) while the
    // battle loaded (bugs 112, 114), and the limit is not documented. Done
    // before the runtime starts any thread, so nothing else is refused while
    // the probe holds them.
    // (The unwrapped calls: these are not counted in "kernel objects live".)
    std::vector<SceUID> made;
    made.reserve(size_t(probe));
    int error = 0;
    while (made.size() < size_t(probe)) {
      const SceUID sema = __real_sceKernelCreateSema("uid_probe", 0, 0, 1, nullptr);
      if (sema < 0) {
        error = sema;
        break;
      }
      made.push_back(sema);
    }
    for (SceUID sema : made) {
      __real_sceKernelDeleteSema(sema);
    }
    if (error) {
      Stats("kernel objects: %u more semaphores could be made at start, then 0x%08X\n",
            unsigned(made.size()), unsigned(error));
    } else {
      Stats("kernel objects: %u more semaphores made at start without a refusal (probe limit)\n",
            unsigned(made.size()));
    }
  }
  // Highest clocks an application may ask for (the default leaves the CPU at
  // 333 MHz): on the console the game's audio thread used a whole core and
  // the loading screen waited for it.
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);
  Stats("clocks: CPU %d MHz, bus %d MHz, GPU %d MHz, GPU crossbar %d MHz\n",
        scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
        scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
  if (const int32_t ballast = REXCVAR_GET(vita_memory_ballast_mib); ballast > 0) {
    // Vita3K has ~80 MiB more main RAM than the console: hold it so a run there
    // meets the console's limits (never freed).
    const SceUID block = sceKernelAllocMemBlock(
        "storm2_ballast", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SceSize(ballast) << 20, nullptr);
    Stats("memory ballast of %d MiB: %s\n", ballast, block >= 0 ? "held" : "FAILED");
    LogMemory("after the ballast");
  }
#if STORM2_CONSOLE
  // On the console the game's memory fits the main RAM (241 MiB free after
  // the executable loaded, ~190 needed on the menus): CDRAM is left to vitaGL,
  // the game may take at most 24 MiB of it once the main RAM is full. (Small
  // blocks first in CDRAM, as tried in the Vita3K simulation, took all of it
  // there and the first console run hung or crashed without GPU memory.)
  // The game's blocks take at most 192 MiB of the main RAM (what the menus
  // used on the console with ~10 MiB left): past that they go to the
  // physically contiguous RAM and CDRAM instead of taking the last of the main
  // RAM, which thread stacks and the shader compiler's block need later.
  // Main RAM: up to 228 MiB for the game's blocks; CDRAM: up to 46 MiB (the
  // vitaGL pool takes 52 MiB and ~8 more outside it). Measured on the console
  // (2026-11-12, menus, small blocks of their own then): 46 MiB of main RAM
  // free with 198 MiB of blocks there and 49.5 MiB of blocks in CDRAM (4 MiB
  // of CDRAM free). A battle needs ~289 MiB of blocks with these caps; ~26
  // more MiB of blocks in the main RAM (cached, faster for the game), 3 MiB
  // more for the RAM pool and 6 for the heap leave ~10 MiB of it free there.
  rex::memory::VitaSetPoolBudgets(size_t(228) << 20, SIZE_MAX, size_t(46) << 20);
#endif
  if (const std::string& spec = REXCVAR_GET(vita_memory_budgets_mib); !spec.empty()) {
    unsigned main_mib = 0, phycont_mib = 0, cdram_mib = 0;
    if (std::sscanf(spec.c_str(), "%u,%u,%u", &main_mib, &phycont_mib, &cdram_mib) == 3) {
      rex::memory::VitaSetPoolBudgets(size_t(main_mib) << 20, size_t(phycont_mib) << 20,
                                      size_t(cdram_mib) << 20);
      Stats("memory budgets for the runtime's blocks: main %u, phycont %u, CDRAM %u MiB\n",
            main_mib, phycont_mib, cdram_mib);
    }
  }
  {
    // How long short sleeps really take (median of 5). In Vita3K on Windows
    // they are close at startup (~1.1 ms for 1 ms) but take 4-15 ms once the
    // game runs (bug 67); not measured on the console yet.
    auto measure = [](SceUInt us) {
      uint64_t t[5];
      for (uint64_t& v : t) {
        const uint64_t start = sceKernelGetProcessTimeWide();
        sceKernelDelayThread(us);
        v = sceKernelGetProcessTimeWide() - start;
      }
      std::sort(std::begin(t), std::end(t));
      return t[2];
    };
    const uint64_t d0 = measure(0), d100 = measure(100), d1000 = measure(1000);
    REXLOG_INFO("sceKernelDelayThread: 0 us -> {} us, 100 us -> {} us, 1000 us -> {} us", d0, d100,
                d1000);
    Stats("sceKernelDelayThread: 0 us -> %llu us, 100 us -> %llu us, 1000 us -> %llu us\n",
          (unsigned long long)d0, (unsigned long long)d100, (unsigned long long)d1000);
  }
  {
    VitaFastTlsReport tls{};
    vita_fast_tls_probe(REXCVAR_GET(vita_fast_tls) ? 1 : 0, &tls);
    vita_fast_tls_set_check(uint32_t(std::max(0, REXCVAR_GET(vita_fast_tls_check))));
    REXLOG_INFO("TLS lookups: TPIDRURO {:08X}/{:08X}, slot 0 at {:+d}, same layout on two threads: "
                "{}, one call {} ns, through TPIDRURO {} ns; fast path {}",
                uint32_t(tls.tp_main), uint32_t(tls.tp_other), int(tls.delta),
                tls.consistent ? "yes" : "no", tls.real_ns, tls.fast_ns,
                tls.enabled ? "on" : "off");
    Stats("tls: layout %s, call %u ns, fast %u ns, fast path %s\n",
          tls.consistent ? "same on two threads" : "differs", tls.real_ns, tls.fast_ns,
          tls.enabled ? "on" : "off");
  }
  {
    // The generated code reads these tables with aligned 16-byte loads; on the
    // console a misaligned one faults at boot (rex/ppc/intrinsics.h).
    const struct {
      const char* name;
      const void* table;
    } tables[] = {{"VectorMaskL", rex::ppc::VectorMaskL},
                  {"VectorMaskR", rex::ppc::VectorMaskR},
                  {"VectorShiftTableL", rex::ppc::VectorShiftTableL},
                  {"VectorShiftTableR", rex::ppc::VectorShiftTableR}};
    for (const auto& t : tables) {
      const uintptr_t address = reinterpret_cast<uintptr_t>(t.table);
      if (address & 15) {
        REXLOG_ERROR("{} at {:08X} is not 16-byte aligned: aligned vector loads fault there",
                     t.name, uint32_t(address));
        Stats("ERROR: %s at %08X is not 16-byte aligned\n", t.name, unsigned(address));
      }
    }
  }

  const std::filesystem::path game_root = std::string(REXCVAR_GET(vita_game_root));
  const std::string xex_host = game_root.string() + "/default.xex";
  SceIoStat xex_stat{};
  if (sceIoGetstat(xex_host.c_str(), &xex_stat) < 0) {
    Fail("default.xex not found in " + game_root.string() +
         " (copy the game folder there, see docs/PLANO_RUNTIME_VITA.md)");
  }
  REXLOG_INFO("Storm 2 (PS Vita, headless): game in {}, xex {} bytes", game_root.string(),
              uint64_t(xex_stat.st_size));

  rex::RuntimeConfig config;
  {
    const std::string gpu = REXCVAR_GET(vita_gpu);
    SceIoStat compiler_stat{};
    const bool have_compiler = sceIoGetstat(kShaderCompilerPath, &compiler_stat) >= 0;
    g_rendering = gpu == "gles" || (gpu == "auto" && have_compiler);
    REXLOG_INFO("GPU: {} (vita_gpu={}, {} {})", g_rendering ? "GLES renderer (vitaGL)" : "null",
                gpu, kShaderCompilerPath, have_compiler ? "present" : "absent");
    Stats("gpu: %s, shader compiler %s\n", g_rendering ? "gles" : "null",
          have_compiler ? "present" : "absent");
    std::unique_ptr<rex::graphics::GraphicsSystem> graphics;
    if (g_rendering) {
      graphics = std::make_unique<rex::graphics::gles::GlesGraphicsSystem>();
    } else {
      graphics = std::make_unique<rex::graphics::null::NullGraphicsSystem>();
    }
    if (XFAILED(graphics->SetupPresentation(nullptr))) {
      Fail("GPU setup failed");
    }
    config.graphics = std::move(graphics);
  }
  config.audio_factory = REX_AUDIO_BACKEND(rex::audio::vita::VitaAudioSystem);
  config.input_factory = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
  config.kernel_init = rex::kernel::InitializeKernel;

  const std::filesystem::path user_root = std::string(kDataDir) + "/user";
  sceIoMkdir(user_root.string().c_str(), 0777);
  auto runtime = std::make_unique<rex::Runtime>(game_root, user_root, std::filesystem::path{},
                                                std::string(kDataDir) + "/cache");
  rex::X_STATUS status = runtime->Setup(PPCImageConfig, std::move(config));
  if (XFAILED(status)) {
    Fail(fmt::format("Runtime setup failed: {:08X}", status));
  }
  LogMemory("runtime ready");

  status = runtime->LoadXexImage("game:\\default.xex");
  if (XFAILED(status)) {
    Fail(fmt::format("Failed to load default.xex: {:08X}", status));
  }
  LogMemory("xex loaded");

  auto main_thread = runtime->PrepareModuleLaunch();
  if (!main_thread) {
    Fail("Failed to launch the game module");
  }
  std::atomic<bool> finished{false};
  main_thread->Resume();
  std::thread waiter([&] {
    main_thread->Wait(0, 0, 0, nullptr);
    finished = true;
  });
  if (!g_rendering) {
    // With the renderer the display belongs to vitaGL.
    Screen(kBlue);
  }
  REXLOG_INFO("Game started");

  // Statistics every 5 s; START + SELECT held for 2 s quits. The sampling
  // mode stays analog: the input driver (vita_input_driver.cpp) reads the
  // sticks, and digital mode would leave them centered.
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  uint64_t last_us = sceKernelGetProcessTimeWide();
  uint32_t last_frame = rex::perf::CurrentGuestFrame();
  uint64_t last_threads_us = last_us;
  uint64_t last_audio_frames = rex::audio::vita::VitaSilentAudioDriver::total_frames_submitted();
  std::map<SceUID, uint64_t> last_thread_run;
  ThreadReport(runtime.get(), last_us, 1, last_thread_run);  // baseline
  const std::string sample_name = REXCVAR_GET(vita_sample_thread);
  if (!sample_name.empty()) {
    g_sampler.memory = runtime->memory();
    g_sampler.all = sample_name == "*";
    size_t start = 0;
    size_t named = 0;
    while (!g_sampler.all && start <= sample_name.size() && named < kMaxSampled) {
      size_t end = sample_name.find(',', start);
      if (end == std::string::npos) {
        end = sample_name.size();
      }
      if (end > start) {
        g_sampler.threads[named++].name = sample_name.substr(start, end - start);
      }
      start = end + 1;
    }
    g_sampler.count.store(named, std::memory_order_release);
    sceIoRemove(kSamplesPath);
    const SceUID sampler = sceKernelCreateThread("storm2_sampler", SamplerThread, 0x40, 0x4000, 0,
                                                 0, nullptr);
    if (sampler >= 0) {
      sceKernelStartThread(sampler, 0, nullptr);
    }
  }
  int quit_ticks = 0;
  int tick = 0;
  // vita_ab_cycle: states of "cvar=value" settings, each on top of the values
  // at start, switched every vita_ab_seconds.
  std::vector<std::vector<std::pair<std::string, std::string>>> ab_states;
  std::vector<std::pair<std::string, std::string>> ab_base;
  {
    const std::string cycle = REXCVAR_GET(vita_ab_cycle);
    size_t pos = 0;
    while (REXCVAR_GET(vita_ab_seconds) > 0 && pos <= cycle.size()) {
      const size_t bar = std::min(cycle.find('|', pos), cycle.size());
      std::vector<std::pair<std::string, std::string>> state;
      const std::string text = cycle.substr(pos, bar - pos);
      size_t p = 0;
      while (p < text.size()) {
        const size_t semi = std::min(text.find(';', p), text.size());
        const std::string item = text.substr(p, semi - p);
        const size_t eq = item.find('=');
        if (eq != std::string::npos && eq > 0) {
          const std::string name = item.substr(0, eq);
          state.emplace_back(name, item.substr(eq + 1));
          bool known = false;
          for (const auto& b : ab_base) {
            known = known || b.first == name;
          }
          if (!known) {
            ab_base.emplace_back(name, rex::cvar::GetFlagByName(name));
          }
        }
        p = semi + 1;
      }
      ab_states.push_back(std::move(state));
      pos = bar + 1;
    }
  }
  size_t ab_index = 0;
  const uint64_t ab_begin_us =
      sceKernelGetProcessTimeWide() + uint64_t(std::max(0, REXCVAR_GET(vita_ab_start_seconds))) * 1000000ull;
  bool ab_started = false;
  uint64_t ab_switch_us = ab_begin_us;
  auto ab_apply = [&](size_t index) {
    std::string text;
    for (const auto& [name, value] : ab_base) {
      rex::cvar::SetFlagByName(name, value);
    }
    for (const auto& [name, value] : ab_states[index]) {
      rex::cvar::SetFlagByName(name, value);
      text += name + "=" + value + " ";
    }
    // Thread cores and priorities are set when a thread starts or asks for
    // them: apply a state's vita_thread_cores, vita_thread_priorities,
    // vita_reserved_core or vita_hw_thread_cores to the running threads too.
    rex::thread::VitaReapplyThreadPlacement();
    REXLOG_INFO("A/B: state {} of {}: {}", index, ab_states.size(),
                text.empty() ? std::string("(values at start)") : text);
    Stats("A/B: state %u of %u: %s\n", unsigned(index), unsigned(ab_states.size()),
          text.empty() ? "(values at start)" : text.c_str());
  };
  const int32_t ab_stop_seconds = REXCVAR_GET(vita_ab_stop_seconds);
  const uint64_t ab_stop_us =
      ab_stop_seconds > 0 ? ab_begin_us - uint64_t(std::max(0, REXCVAR_GET(vita_ab_start_seconds))) * 1000000ull +
                                uint64_t(ab_stop_seconds) * 1000000ull
                          : UINT64_MAX;
  while (!finished) {
    if (ab_states.size() > 1) {
      const uint64_t now_us = sceKernelGetProcessTimeWide();
      if (now_us >= ab_stop_us) {
        // Back to the first state for the rest of the run.
        if (ab_started && ab_index != 0) {
          ab_index = 0;
          ab_apply(0);
        }
        REXLOG_INFO("A/B: stopped at state 0 after {} s", ab_stop_seconds);
        Stats("A/B: stopped at state 0\n");
        ab_states.clear();
      } else if (!ab_started) {
        if (now_us >= ab_begin_us) {
          ab_started = true;
          ab_switch_us = now_us;
          ab_apply(0);
        }
      } else if (now_us - ab_switch_us >= uint64_t(REXCVAR_GET(vita_ab_seconds)) * 1000000ull) {
        ab_switch_us = now_us;
        ab_index = (ab_index + 1) % ab_states.size();
        ab_apply(ab_index);
      }
    }
    if (g_sampler.all && tick % 10 == 0) {
      // Every guest thread, as threads appear (checked once a second).
      for (auto& xthread : runtime->kernel_state()->object_table()
                               ->GetObjectsByType<rex::system::XThread>()) {
        rex::thread::Thread* thread = xthread->thread();
        if (!thread || !xthread->thread_state()) {
          continue;
        }
        PPCContext* ctx = xthread->thread_state()->context();
        const size_t count = g_sampler.count.load(std::memory_order_relaxed);
        bool known = false;
        for (size_t n = 0; n < count; ++n) {
          known = known || g_sampler.threads[n].ctx.load() == ctx;
        }
        if (!known && count < kMaxSampled) {
          g_sampler.threads[count].name = thread->name();
          g_sampler.threads[count].stack_low = xthread->stack_limit();
          g_sampler.threads[count].stack_high = xthread->stack_base();
          g_sampler.threads[count].ctx.store(ctx);
          g_sampler.count.store(count + 1, std::memory_order_release);
        }
      }
    }
    for (size_t n = 0; !g_sampler.all && n < g_sampler.count.load(); ++n) {
      SampledThread& t = g_sampler.threads[n];
      if (t.ctx.load()) {
        continue;
      }
      // The guest thread to sample, once it exists.
      for (auto& xthread : runtime->kernel_state()->object_table()
                               ->GetObjectsByType<rex::system::XThread>()) {
        rex::thread::Thread* thread = xthread->thread();
        if (thread && thread->name().find(t.name) != std::string::npos &&
            xthread->thread_state()) {
          t.stack_low = xthread->stack_limit();
          t.stack_high = xthread->stack_base();
          t.ctx.store(xthread->thread_state()->context());
          REXLOG_INFO("Sampling the link register of guest thread '{}'", thread->name());
          break;
        }
      }
    }
    sceKernelDelayThread(100 * 1000);
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    quit_ticks = ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_SELECT))
                     ? quit_ticks + 1
                     : 0;
    if (quit_ticks >= 20) {
      REXLOG_INFO("START + SELECT: quitting");
      break;
    }
    if (++tick % 50 != 0) {
      continue;
    }
    const uint64_t now_us = sceKernelGetProcessTimeWide();
    const uint32_t frame = rex::perf::CurrentGuestFrame();
    rex::memory::GuestMemoryStats mem{};
    runtime->memory()->GetGuestMemoryStats(mem);
    SceKernelFreeMemorySizeInfo free_mem{};
    free_mem.size = sizeof(free_mem);
    sceKernelGetFreeMemorySize(&free_mem);
    const struct mallinfo heap = mallinfo();
    const uint64_t audio_frames = rex::audio::vita::VitaSilentAudioDriver::total_frames_submitted();
    Stats("t=%llus frames=%u fps=%.2f audio=%.1f/s backed=%.1fMiB guest_committed=%.1fMiB "
          "free_user=%dKiB free_cdram=%dKiB free_phycont=%dKiB heap_used=%.1fMiB "
          "heap_free=%.1fMiB heap_arena=%.1fMiB\n",
          (unsigned long long)(now_us / 1000000), frame,
          double(frame - last_frame) * 1e6 / double(now_us - last_us),
          double(audio_frames - last_audio_frames) * 1e6 / double(now_us - last_us),
          double(mem.host_backed) / 1048576.0,
          double(mem.virtual_committed + mem.physical_committed) / 1048576.0,
          free_mem.size_user / 1024, free_mem.size_cdram / 1024, free_mem.size_phycont / 1024,
          double(heap.uordblks) / 1048576.0, double(heap.fordblks) / 1048576.0,
          double(heap.arena) / 1048576.0);
    {
      size_t pools[3];
      rex::memory::VitaPoolUsage(pools);
      // Kernel memory blocks: each is a kernel object, and the console limits
      // how many a process holds (SCE_KERNEL_ERROR_UID_RL_OVERFLOW).
      size_t kernel_blocks = 0, arenas = 0;
      rex::memory::VitaBlockCounts(&kernel_blocks, &arenas);
      Stats("  runtime blocks: main %.1f MiB, phycont %.1f MiB, CDRAM %.1f MiB; %u kernel memory "
            "blocks (%u arenas of small blocks)\n",
            double(pools[0]) / 1048576.0, double(pools[1]) / 1048576.0,
            double(pools[2]) / 1048576.0, unsigned(kernel_blocks), unsigned(arenas));
    }
    {
      // Live kernel objects by kind (compat/kernel_objects.c). A kind whose
      // count only grows is a leak that ends at the console's limit (bug 114).
      VitaKernelObjectCount kinds[16];
      const int n = std::min(vita_kernel_object_counts(kinds, 16), 16);
      std::string line = "  kernel objects live (made):";
      uint32_t failed = 0;
      int32_t last_error = 0;
      for (int i = 0; i < n; ++i) {
        line += fmt::format(" {} {} ({})", kinds[i].name, kinds[i].live, kinds[i].made);
        if (kinds[i].failed) {
          failed += kinds[i].failed;
          last_error = kinds[i].last_error;
        }
      }
      uint32_t mutexes_made = 0, mutexes_destroyed = 0;
      vita_pthread_mutex_counts(&mutexes_made, &mutexes_destroyed);
      line += fmt::format("; pthread mutexes {} made, {} destroyed; host thread objects {}",
                          mutexes_made, mutexes_destroyed,
                          unsigned(rex::thread::VitaLiveThreadObjects()));
      if (failed) {
        line += fmt::format("; {} creations FAILED (last {:08X})", failed, uint32_t(last_error));
      }
      VitaMutexDoubleDestroy doubles[8];
      if (const int n_doubles = vita_mutex_double_destroys(doubles, 8); n_doubles > 0) {
        // A mutex destroyed twice: its owner was destroyed twice or after it
        // was freed (arm-vita-eabi-addr2line -f -e storm2_vita <caller>).
        line += fmt::format("; {} mutexes destroyed again:", n_doubles);
        for (int i = 0; i < std::min(n_doubles, 8); ++i) {
          line += fmt::format(" [{:08X} by {:08X}, first by {:08X}]", uint32_t(doubles[i].mutex),
                              uint32_t(doubles[i].caller), uint32_t(doubles[i].first_caller));
        }
      }
      Stats("%s\n", line.c_str());
    }
    {
      // Guest kernel objects by type. One the game uses through a structure
      // of its own (KeSetEvent on a KEVENT it initialized inline) gets a host
      // object at first use with no guest handle that closes it: counted in
      // parentheses (no guest object of its own). Each host event, semaphore or
      // thread holds kernel semaphores (bug 114).
      static const char* const kTypeNames[] = {
          "undefined", "enumerator", "event",   "file",    "io completion",
          "module",    "mutant",     "notify",  "semaphore", "session",
          "socket",    "symlink",    "thread",  "timer"};
      constexpr uint32_t kTypes = sizeof(kTypeNames) / sizeof(kTypeNames[0]);
      uint32_t total[kTypes] = {}, native[kTypes] = {};
      for (auto& object : runtime->kernel_state()->object_table()->GetAllObjects()) {
        const uint32_t type = uint32_t(object->type());
        if (type < kTypes) {
          ++total[type];
          native[type] += object->guest_object() ? 0 : 1;
        }
      }
      std::string line = "  guest objects (without a guest object of their own):";
      for (uint32_t type = 0; type < kTypes; ++type) {
        if (total[type]) {
          line += fmt::format(" {} {} ({})", kTypeNames[type], total[type], native[type]);
        }
      }
      Stats("%s\n", line.c_str());
    }
    {
      // Multi-object and alertable waits that blocked, per second.
      static uint64_t last_polls[3] = {};
      uint64_t polls[3];
      rex::thread::VitaPollWakeups(polls);
      const double seconds = double(now_us - last_us) / 1e6;
      Stats("  multi-object/alertable waits that blocked: %.0f/s\n",
            double(polls[0] - last_polls[0]) / seconds);
      std::copy(std::begin(polls), std::end(polls), std::begin(last_polls));
      static uint64_t last_tls_calls = 0;
      VitaFastTlsStats tls{};
      vita_fast_tls_stats(&tls);
      Stats("  TLS lookups: %.0f/s (fast path %s, %u mismatches)\n",
            double(tls.calls - last_tls_calls) / seconds, tls.enabled ? "on" : "off",
            tls.mismatches);
      last_tls_calls = tls.calls;
      // Other frequent system calls, per second.
      static uint64_t last_calls[VITA_SYSCALL_COUNTERS_MAX] = {};
      uint64_t calls[VITA_SYSCALL_COUNTERS_MAX];
      const char* const* names = nullptr;
      const int n = vita_syscall_counts(calls, &names);
      std::string line = "  system calls/s:";
      for (int i = 0; i < n; ++i) {
        line += fmt::format(" {} {:.0f}", names[i], double(calls[i] - last_calls[i]) / seconds);
        last_calls[i] = calls[i];
      }
      Stats("%s\n", line.c_str());
    }
    last_us = now_us;
    last_frame = frame;
    last_audio_frames = audio_frames;
    // Every 10 s (a hang on the console is often closed within a minute).
    if (now_us - last_threads_us >= 10 * 1000000ull) {
      // Where the frequent system calls come from (return addresses in this
      // executable; arm-vita-eabi-addr2line -f -e storm2_vita <address>).
      uint64_t unused[VITA_SYSCALL_COUNTERS_MAX];
      const char* const* names = nullptr;
      const int kinds = vita_syscall_counts(unused, &names);
      for (int kind = 0; kind < kinds; ++kind) {
        uintptr_t addrs[8];
        uint32_t counts[8];
        const int n = vita_syscall_top_sites(kind, addrs, counts, 8);
        if (n == 0) {
          continue;
        }
        std::string line = fmt::format("  sites of {} (since start):", names[kind]);
        for (int i = 0; i < n; ++i) {
          line += fmt::format(" {:08X} x{}", uint32_t(addrs[i]), counts[i]);
        }
        Stats("%s\n", line.c_str());
      }
      ThreadReport(runtime.get(), now_us, now_us - last_threads_us, last_thread_run);
      last_threads_us = now_us;
      WriteSamples(now_us);
      if (vita_heap_trace_on()) {
        char title[64];
        snprintf(title, sizeof(title), "t=%llus frame %u", (unsigned long long)(now_us / 1000000),
                 frame);
        vita_heap_trace_dump(kHeapTracePath, title);
      }
    }
  }

  if (!g_rendering) {
    Screen(kGreen);
  }
  rex::perf::DumpImportStats();
  rex::FlushLogging();
  if (finished) {
    waiter.join();
    REXLOG_INFO("Game ended");
  }
  rex::FlushLogging();
  sceKernelExitProcess(0);
  return 0;
}

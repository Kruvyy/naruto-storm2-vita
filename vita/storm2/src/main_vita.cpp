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
#include <atomic>
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

#include <rex/audio/vita/vita_audio_system.h>
#include <rex/cvar.h>
#include <rex/graphics/gles/gles_gpu.h>
#include <rex/graphics/null/null_gpu.h>
#include <rex/image_info.h>
#include <rex/input/input_system.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/perf/guest_frame_stats.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

// Generated with the recompiled code (storm2_init.cpp).
extern const rex::PPCImageInfo PPCImageConfig;

// newlib's malloc heap is one block taken at start-up; the guest memory comes
// from memory blocks, so keep this moderate.
extern "C" {
unsigned int _newlib_heap_size_user = 48 * 1024 * 1024;
}

REXCVAR_DEFINE_STRING(vita_game_root, "ux0:data/storm2port/game", "Vita",
                      "Folder with default.xex and the game data");
REXCVAR_DEFINE_STRING(vita_gpu, "auto", "Vita",
                      "GPU backend: gles (renderer, needs ur0:data/external/libshacccg.suprx), "
                      "null (no rendering), auto (gles if the shader compiler is installed)");

namespace {
constexpr const char* kShaderCompilerPath = "ur0:data/external/libshacccg.suprx";
bool g_rendering = false;
}  // namespace

namespace {

constexpr const char* kDataDir = "ux0:data/storm2port";
constexpr const char* kStatsPath = "ux0:data/storm2port/storm2_stats.log";
constexpr const char* kFrameStatsPath = "ux0:data/storm2port/frame_stats.csv";
constexpr const char* kImportStatsPath = "ux0:data/storm2port/import_stats.csv";

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
  };
  std::vector<Row> rows;
  std::map<SceUID, uint64_t> run;
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
    const uint64_t run_us = info.runClocks;
    run[uid] = run_us;
    auto it = last_run.find(uid);
    const uint64_t delta = it != last_run.end() && run_us >= it->second ? run_us - it->second : 0;
    rows.push_back({xthread->name().empty() ? std::string(info.name) : xthread->name(),
                    xthread->thread_id(), double(delta) / double(interval_us), info.lastExecutedCpuId,
                    info.currentPriority, run_us});
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
  for (const Row& row : rows) {
    if (row.share < 0.005) {
      continue;
    }
    text += fmt::format("  {:5.1f}%  core {}  prio {:3}  {:08X}  {}\n", row.share * 100, row.core,
                        row.priority, row.guest_id, row.name);
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

  // cvars: defaults for the Vita, then the optional storm2.toml.
  char arg0[] = "storm2";
  char* argv[] = {arg0, nullptr};
  rex::cvar::Init(1, argv);
  rex::cvar::SetFlagByName("perf_frame_stats", "true");
  rex::cvar::SetFlagByName("perf_frame_stats_file", kFrameStatsPath);
  rex::cvar::SetFlagByName("perf_import_stats_file", kImportStatsPath);
  // The physical pool at 0xAB000000 reserves 272 MiB and the game fills it from
  // the start; 184 MiB ran the title screen on the PC (docs/logs/E13d). A write
  // past the limit stops the game with a message naming the cvar.
  rex::cvar::SetFlagByName("soft_mmu_caps", "0xAB000000=184");
  rex::cvar::LoadConfig(std::string(kDataDir) + "/storm2.toml");

  {
    rex::LogConfig log_config;
    const std::string log_path = std::string(kDataDir) + "/storm2.log";
    sceIoRemove(log_path.c_str());
    log_config.log_file = log_path.c_str();
    rex::InitLogging(log_config);
  }
  rex::cvar::FinalizeInit();
  LogMemory("start");

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

  // Statistics every 5 s; START + SELECT held for 2 s quits.
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
  uint64_t last_us = sceKernelGetProcessTimeWide();
  uint32_t last_frame = rex::perf::CurrentGuestFrame();
  uint64_t last_threads_us = last_us;
  uint64_t last_audio_frames = rex::audio::vita::VitaSilentAudioDriver::total_frames_submitted();
  std::map<SceUID, uint64_t> last_thread_run;
  ThreadReport(runtime.get(), last_us, 1, last_thread_run);  // baseline
  int quit_ticks = 0;
  int tick = 0;
  while (!finished) {
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
    last_us = now_us;
    last_frame = frame;
    last_audio_frames = audio_frames;
    if (now_us - last_threads_us >= 30 * 1000000ull) {
      ThreadReport(runtime.get(), now_us, now_us - last_threads_us, last_thread_run);
      last_threads_us = now_us;
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

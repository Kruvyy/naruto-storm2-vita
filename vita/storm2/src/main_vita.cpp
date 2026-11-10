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

#include <fast_tls.h>    // vita/runtime/compat
#include <heap_trace.h>  // vita/runtime/compat

namespace rex::thread {
void VitaPollWakeups(uint64_t out[3]);  // core/threading_vita.cpp
uint64_t VitaThreadBlockedUs(uint32_t system_id);  // core/threading_vita.cpp
}

// newlib's malloc heap is one block taken at start-up; the guest memory comes
// from memory blocks. 48 MiB ran out in battle in Vita3K (GLES vertex staging
// buffers, XMA decoders; bug 90); the target is Vita3K now (the game does not
// fit the console's memory, docs/bugs.md), where ~320 MiB are free at start.
extern "C" {
unsigned int _newlib_heap_size_user = 96 * 1024 * 1024;
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
  };
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
    text += fmt::format("  {:5.1f}%  core {}  prio {:3}  hw {} mask {:02X}  {:08X}  {}\n",
                        row.share * 100, row.core, row.priority, row.guest_cpu,
                        unsigned(row.mask), row.guest_id, row.name);
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
    if (now_us - last_threads_us >= 30 * 1000000ull) {
      // Where the frequent system calls come from (return addresses in this
      // executable; arm-vita-eabi-addr2line -f -e storm2_vita <address>).
      uint64_t unused[VITA_SYSCALL_COUNTERS_MAX];
      const char* const* names = nullptr;
      const int kinds = vita_syscall_counts(unused, &names);
      for (int kind = 0; kind < kinds; ++kind) {
        uintptr_t addrs[4];
        uint32_t counts[4];
        const int n = vita_syscall_top_sites(kind, addrs, counts, 4);
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

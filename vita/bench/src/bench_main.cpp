// Codegen register-model benchmark (PS Vita and desktop host).
//
// The bench.* cases (vita/ppc_tests/tools/gen_bench.py: synthetic loops plus
// game-like struct/vec3 code) are generated once per codegen variant
// (vita/bench/tools/make_variants.py). This program:
//   1. checks every case of every variant against the expected values of the
//      independent Python model (a faster variant that computes something else
//      is useless);
//   2. times each case for each variant, interleaving variants and repeating
//      the whole sweep 3 times, keeping the best time per variant (so clock or
//      thermal changes during the run do not favour one variant).
// Memory model as in vita/ppc_tests: REX_GUEST_SOFT_MMU, one real 64 KiB test
// page, everything else a guard page that must stay zero.
//
// Output: ux0:data/storm2port/bench.log (Vita) or bench_host.log (desktop).
// Vita screen: yellow while running, green (all correct) or red; any button
// exits.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "vita_harness.h"
#include "variants.h"

#include <rex/ppc/func.h>
#include <rex/system/mmio_handler.h>

#if defined(__vita__)
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#else
#include <chrono>
#endif

namespace {

constexpr uint32_t kPageSize = 0x10000;
constexpr uint32_t kTestPage = 0x1000;

alignas(64) uint8_t* g_table[65536];
alignas(64) uint8_t g_test_page[kPageSize];
alignas(64) uint8_t g_guard_page[kPageSize];
FILE* g_log = nullptr;
const char* g_current_test = "";

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (g_log) {
    va_list ap2;
    va_copy(ap2, ap);
    vfprintf(g_log, fmt, ap2);
    va_end(ap2);
    fflush(g_log);
  }
#if !defined(__vita__)
  vprintf(fmt, ap);
#endif
  va_end(ap);
}

bool GuardDirty() {
  for (uint32_t i = 0; i < kPageSize; ++i)
    if (g_guard_page[i] != 0)
      return true;
  return false;
}

#if defined(__vita__)
constexpr int kScreenW = 960, kScreenH = 544;
uint32_t* g_fb = nullptr;

void ScreenInit() {
  const unsigned size = (kScreenW * kScreenH * 4 + 0x3FFFF) & ~0x3FFFFu;
  SceUID block = sceKernelAllocMemBlock("bench_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size,
                                        nullptr);
  if (block < 0)
    return;
  void* base = nullptr;
  sceKernelGetMemBlockBase(block, &base);
  g_fb = static_cast<uint32_t*>(base);
  SceDisplayFrameBuf fb{};
  fb.size = sizeof(fb);
  fb.base = g_fb;
  fb.pitch = kScreenW;
  fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
  fb.width = kScreenW;
  fb.height = kScreenH;
  sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

void Fill(uint32_t color, int y0, int y1) {
  if (!g_fb)
    return;
  for (int y = y0; y < y1; ++y)
    for (int x = 0; x < kScreenW; ++x)
      g_fb[y * kScreenW + x] = color;
}

void Progress(unsigned done, unsigned total) {
  if (!g_fb || total == 0)
    return;
  const int w = static_cast<int>((uint64_t)kScreenW * done / total);
  for (int y = kScreenH - 40; y < kScreenH; ++y)
    for (int x = 0; x < w; ++x)
      g_fb[y * kScreenW + x] = 0xFF20C020u;
}

void WaitButtonAndExit(int code) {
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
  SceCtrlData pad{};
  for (int i = 0; i < 30; ++i)
    sceKernelDelayThread(16 * 1000);
  for (;;) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (pad.buttons != 0)
      break;
    sceKernelDelayThread(16 * 1000);
  }
  sceKernelExitProcess(code);
}
#endif

}  // namespace

extern "C" PPCFunc* const* rex_guest_func_table = nullptr;

uint8_t* vita_guest_base() { return reinterpret_cast<uint8_t*>(g_table); }
uint8_t* vita_guest_ptr(uint32_t a) { return g_table[a >> 16] + (a & (kPageSize - 1)); }

extern "C" void vita_harness_log(const char* level, const char* msg) {
  Log("[%s] (%s) %s\n", level, g_current_test, msg);
}

extern "C" uint64_t vita_harness_time_us(void) {
#if defined(__vita__)
  return sceKernelGetProcessTimeWide();
#else
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

namespace rex::runtime {
PPCFunc* ResolveIndirectFunction(uint32_t guest_address) {
  Log("[FATAL] (%s) indirect call to unregistered guest function 0x%08X\n", g_current_test,
      guest_address);
  std::abort();
}
MMIOHandler* MMIOHandler::global_handler() {
  static MMIOHandler handler;
  return &handler;
}
bool MMIOHandler::CheckLoad(uint32_t address, uint32_t*) {
  Log("[FATAL] (%s) MMIO load 0x%08X\n", g_current_test, address);
  std::abort();
}
bool MMIOHandler::CheckStore(uint32_t address, uint32_t) {
  Log("[FATAL] (%s) MMIO store 0x%08X\n", g_current_test, address);
  std::abort();
}
}  // namespace rex::runtime

namespace {

// Returns true when the case ran and all its checks passed.
bool RunOne(const VitaTestEntry& t, bool quiet) {
  g_current_test = t.name;
  VitaTest vt;
  bool ok = true;
  try {
    t.fn(vt);
  } catch (const std::exception& e) {
    if (!quiet)
      Log("FAIL %s: exception: %s\n", t.name, e.what());
    ok = false;
  } catch (...) {
    if (!quiet)
      Log("FAIL %s: unknown exception\n", t.name);
    ok = false;
  }
  if (ok && vt.failed) {
    if (!quiet)
      Log("FAIL %s: line %d: %s\n", t.name, vt.line, vt.expr);
    ok = false;
  }
  if (GuardDirty()) {
    if (!quiet)
      Log("FAIL %s: wrote outside the test page\n", t.name);
    std::memset(g_guard_page, 0, sizeof(g_guard_page));
    ok = false;
  }
  return ok;
}

// Short case name without the "vN.bench.test_bench_" prefix.
const char* ShortName(const char* full) {
  const char* p = std::strstr(full, "test_bench_");
  return p ? p + 11 : full;
}

// Executed PPC instructions per call (printed by gen_bench.py).
uint32_t ExecutedPerCall(const char* shortname) {
  struct { const char* n; uint32_t c; } k[] = {
      {"int", 73733}, {"fp", 16392}, {"fma", 12296}, {"struct", 135241}, {"vec3", 10360}};
  for (auto& e : k)
    if (std::strcmp(e.n, shortname) == 0)
      return e.c;
  return 0;
}

unsigned Reps(const char* shortname) {
  if (std::strcmp(shortname, "struct") == 0)
    return 10;
  if (std::strcmp(shortname, "int") == 0)
    return 20;
  return 40;
}

}  // namespace

int main() {
#if defined(__vita__)
  sceIoMkdir("ux0:data", 0777);
  sceIoMkdir("ux0:data/storm2port", 0777);
  sceIoRemove("ux0:data/storm2port/bench.log");
  g_log = std::fopen("ux0:data/storm2port/bench.log", "w");
  ScreenInit();
  Fill(0xFF00D0D0u, 0, 544);  // yellow: running
#else
  g_log = std::fopen("bench_host.log", "w");
#endif
  for (auto& p : g_table)
    p = g_guard_page;
  g_table[kTestPage] = g_test_page;

#if defined(__vita__)
  Log("ReXGlue codegen register-model benchmark - PS Vita (ARMv7, %s)\n", __VERSION__);
  Log("clocks reported by the OS: ARM %d MHz, bus %d MHz, GPU %d MHz (an overclock plugin may "
      "change these during the run)\n",
      scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
      scePowerGetGpuClockFrequency());
#else
  Log("ReXGlue codegen register-model benchmark - desktop host (%s)\n", __VERSION__);
#endif
  for (unsigned v = 0; v < kBenchVariantCount; ++v)
    Log("variant %s: %s\n", kBenchVariants[v].id, kBenchVariants[v].desc);

  // 1. Correctness of every case of every variant.
  unsigned failed = 0, total = 0;
  for (unsigned v = 0; v < kBenchVariantCount; ++v) {
    for (unsigned i = 0; i < kBenchVariants[v].count; ++i) {
      ++total;
      if (!RunOne(kBenchVariants[v].cases[i], false))
        ++failed;
    }
  }
  Log("CHECK passed=%u failed=%u total=%u\n", total - failed, failed, total);

  // 2. Timing: best of 3 sweeps, variants interleaved per case.
  const unsigned ncases = kBenchVariants[0].count;
  static double best[8][16];
  for (auto& row : best)
    for (auto& x : row)
      x = 1e30;
  const unsigned kRounds = 3;
  unsigned step = 0, steps = kRounds * ncases * kBenchVariantCount;
  for (unsigned round = 0; round < kRounds; ++round) {
    for (unsigned c = 0; c < ncases; ++c) {
      const char* sn = ShortName(kBenchVariants[0].cases[c].name);
      if (std::strcmp(sn, "leaf") == 0)
        continue;
      for (unsigned v = 0; v < kBenchVariantCount; ++v) {
        const VitaTestEntry& t = kBenchVariants[v].cases[c];
        const unsigned reps = Reps(sn);
        const uint64_t t0 = vita_harness_time_us();
        for (unsigned k = 0; k < reps; ++k)
          RunOne(t, true);
        const double us = double(vita_harness_time_us() - t0) / reps;
        if (us < best[v][c])
          best[v][c] = us;
#if defined(__vita__)
        Progress(++step, steps);
#else
        ++step;
#endif
      }
    }
  }

  Log("BENCH best of %u sweeps; us per call (M guest instr/s) [speedup vs v0]\n", kRounds);
  for (unsigned c = 0; c < ncases; ++c) {
    const char* sn = ShortName(kBenchVariants[0].cases[c].name);
    if (std::strcmp(sn, "leaf") == 0)
      continue;
    const uint32_t n = ExecutedPerCall(sn);
    Log("BENCH %-7s", sn);
    for (unsigned v = 0; v < kBenchVariantCount; ++v) {
      const double us = best[v][c];
      Log(" | %s %9.1f us (%6.1f M/s) [%4.2fx]", kBenchVariants[v].id, us, n ? n / us : 0.0,
          best[0][c] / us);
    }
    Log("\n");
  }
  if (g_log)
    std::fclose(g_log);
#if defined(__vita__)
  Fill(failed ? 0xFF1010C0u : 0xFF10A010u, 0, 544);
  WaitButtonAndExit(0);
#endif
  return failed ? 1 : 0;
}

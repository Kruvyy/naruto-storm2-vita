// Runner for recompiled PPC instruction tests (PS Vita and desktop host).
//
// Memory model: REX_GUEST_SOFT_MMU. Guest page 0x1000 (0x10000000-0x1000FFFF,
// the only page the tests touch) is real memory; every other page maps to a
// guard page that must stay zero, so a stray access fails the test instead of
// silently passing or crashing the process.
//
// Output: log file (Vita: ux0:data/storm2port/ppc_tests.log) with every
// failure, timings and a benchmark section. On the Vita the screen turns
// yellow while running, then green (all passed) or red (failures); any
// button exits back to LiveArea.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "vita_harness.h"

#include <rex/ppc/func.h>
#include <rex/system/mmio_handler.h>

#if defined(__vita__)
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
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

bool GuardDirty() {
  for (uint32_t i = 0; i < kPageSize; ++i)
    if (g_guard_page[i] != 0)
      return true;
  return false;
}

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

#if defined(__vita__)
constexpr int kScreenW = 960, kScreenH = 544;
uint32_t* g_fb = nullptr;

void ScreenInit() {
  const unsigned size = (kScreenW * kScreenH * 4 + 0x3FFFF) & ~0x3FFFFu;
  SceUID block = sceKernelAllocMemBlock("ppc_tests_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                        size, nullptr);
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

// A8B8G8R8 little-endian word: 0xAABBGGRR.
void Fill(uint32_t color, int y0, int y1) {
  if (!g_fb)
    return;
  for (int y = y0; y < y1; ++y)
    for (int x = 0; x < kScreenW; ++x)
      g_fb[y * kScreenW + x] = color;
}

void Progress(unsigned done, unsigned total, unsigned failed) {
  if (!g_fb || total == 0)
    return;
  const int w = static_cast<int>((uint64_t)kScreenW * done / total);
  const uint32_t color = failed ? 0xFF2020E0u : 0xFF20C020u;
  for (int y = kScreenH - 40; y < kScreenH; ++y)
    for (int x = 0; x < w; ++x)
      g_fb[y * kScreenW + x] = color;
}

void WaitButtonAndExit(int code) {
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
  SceCtrlData pad{};
  // Ignore buttons still held from launching the app.
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
  Log("[FATAL] (%s) MMIO load 0x%08X not supported by the test harness\n", g_current_test,
      address);
  std::abort();
}
bool MMIOHandler::CheckStore(uint32_t address, uint32_t) {
  Log("[FATAL] (%s) MMIO store 0x%08X not supported by the test harness\n", g_current_test,
      address);
  std::abort();
}
}  // namespace rex::runtime

namespace {

struct Result {
  unsigned passed = 0, failed = 0, exceptions = 0, guard = 0;
};

uint64_t g_exec_us = 0;  // time spent inside test functions only

bool RunOne(const VitaTestEntry& t, Result& r, bool quiet) {
  g_current_test = t.name;
  if (!quiet)
    Log("RUN %s\n", t.name);  // flushed: the last RUN line names a crashing test
  VitaTest vt;
  bool ok = true;
  const uint64_t s0 = vita_harness_time_us();
  try {
    t.fn(vt);
    g_exec_us += vita_harness_time_us() - s0;
  } catch (const std::exception& e) {
    g_exec_us += vita_harness_time_us() - s0;
    if (!quiet)
      Log("FAIL %s: exception: %s\n", t.name, e.what());
    r.exceptions++;
    ok = false;
  } catch (...) {
    g_exec_us += vita_harness_time_us() - s0;
    if (!quiet)
      Log("FAIL %s: unknown exception\n", t.name);
    r.exceptions++;
    ok = false;
  }
  if (ok && vt.failed) {
    if (!quiet)
      Log("FAIL %s: line %d: %s\n", t.name, vt.line, vt.expr);
    ok = false;
  }
  if (GuardDirty()) {
    if (!quiet)
      Log("FAIL %s: wrote outside the mapped test page\n", t.name);
    std::memset(g_guard_page, 0, sizeof(g_guard_page));
    r.guard++;
    ok = false;
  }
  if (ok)
    r.passed++;
  else
    r.failed++;
  return ok;
}

}  // namespace

int main() {
#if defined(__vita__)
  sceIoMkdir("ux0:data", 0777);
  sceIoMkdir("ux0:data/storm2port", 0777);
  g_log = std::fopen("ux0:data/storm2port/ppc_tests.log", "w");
  ScreenInit();
  Fill(0xFF00D0D0u, 0, 544);  // yellow: running
#else
  g_log = std::fopen("ppc_tests_harness.log", "w");
#endif

  for (auto& p : g_table)
    p = g_guard_page;
  g_table[kTestPage] = g_test_page;

  Log("ReXGlue PPC instruction tests, soft MMU + split u64, %u cases\n", kVitaTestCount);
#if defined(__vita__)
  Log("host: PS Vita (ARMv7, %s)\n", __VERSION__);
#else
  Log("host: desktop (%s)\n", __VERSION__);
#endif

  Result r;
  const uint64_t t0 = vita_harness_time_us();
  for (unsigned i = 0; i < kVitaTestCount; ++i) {
    RunOne(kVitaTests[i], r, false);
#if defined(__vita__)
    if ((i & 31) == 0 || i + 1 == kVitaTestCount)
      Progress(i + 1, kVitaTestCount, r.failed);
#endif
  }
  const uint64_t t1 = vita_harness_time_us();
  Log("RESULT passed=%u failed=%u (exceptions=%u guard=%u) total=%u exec_us=%llu wall_us=%llu\n",
      r.passed, r.failed, r.exceptions, r.guard, kVitaTestCount,
      (unsigned long long)g_exec_us, (unsigned long long)(t1 - t0));

  // Benchmark: repeat the bench.* cases (also checked for correctness above).
  Log("BENCH (each case repeated, time per call in microseconds)\n");
  for (unsigned i = 0; i < kVitaTestCount; ++i) {
    if (std::strncmp(kVitaTests[i].name, "bench.", 6) != 0)
      continue;
    const unsigned reps = 50;
    Result br;
    const uint64_t b0 = vita_harness_time_us();
    for (unsigned k = 0; k < reps; ++k)
      RunOne(kVitaTests[i], br, true);
    const uint64_t b1 = vita_harness_time_us();
    Log("BENCH %s reps=%u failed=%u us_per_call=%.1f\n", kVitaTests[i].name, reps, br.failed,
        double(b1 - b0) / reps);
  }

  if (g_log)
    std::fclose(g_log);
#if defined(__vita__)
  Fill(r.failed ? 0xFF1010C0u : 0xFF10A010u, 0, 544);  // red or green
  WaitButtonAndExit(0);
#endif
  return r.failed ? 1 : 0;
}

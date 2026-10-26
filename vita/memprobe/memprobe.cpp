// PS Vita memory probe: how much memory an app can get, in which kinds of
// block, and how fast the CPU reads and writes each kind.
//
// Built twice by CMakeLists.txt: a normal app (RXGT00004) and one with the
// extended-memory attribute (ATTRIBUTE2=12 in param.sfo, RXGT00005). Each
// writes ux0:data/storm2port/memprobe_<variant>.log. The screen turns yellow
// while running, then green; any button exits.

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#ifndef MEMPROBE_VARIANT
#define MEMPROBE_VARIANT "normal"
#endif

// Keep newlib's own heap small so the probe sees what memory blocks can get.
extern "C" {
unsigned int _newlib_heap_size_user = 8 * 1024 * 1024;
}

namespace {

const char* kLogPath = "ux0:data/storm2port/memprobe_" MEMPROBE_VARIANT ".log";

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return;
  }
  SceUID fd = sceIoOpen(kLogPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd < 0) {
    return;
  }
  sceIoWrite(fd, line, std::min<size_t>(size_t(n), sizeof(line) - 1));
  sceIoClose(fd);
}

uint64_t NowUs() {
  return sceKernelGetProcessTimeWide();
}

void LogFree(const char* when) {
  SceKernelFreeMemorySizeInfo mem{};
  mem.size = sizeof(mem);
  if (sceKernelGetFreeMemorySize(&mem) >= 0) {
    Log("free %-12s user %7d KiB  cdram %7d KiB  phycont %7d KiB\n", when, mem.size_user / 1024,
        mem.size_cdram / 1024, mem.size_phycont / 1024);
  }
}

struct Kind {
  const char* name;
  SceKernelMemBlockType type;
  uint32_t granule;  // size alignment the kernel requires
};

const Kind kKinds[] = {
    {"USER_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 4096},
    {"USER_MAIN_GAME_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_GAME_RW, 4096},
    {"USER_MAIN_PHYCONT_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, 1024 * 1024},
    {"USER_CDRAM_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 256 * 1024},
    {"USER_CDRAM_L1WBWA_RW", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_L1WBWA_RW, 256 * 1024},
    {"USER_RW_UNCACHE", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4096},
};

SceUID Alloc(const Kind& kind, uint32_t size, void** out) {
  SceUID uid = sceKernelAllocMemBlock("probe", kind.type, size, nullptr);
  if (uid < 0) {
    return uid;
  }
  if (sceKernelGetMemBlockBase(uid, out) < 0) {
    sceKernelFreeMemBlock(uid);
    return -1;
  }
  return uid;
}

// Largest single block of this kind (binary search in granule steps).
uint32_t LargestBlock(const Kind& kind) {
  uint32_t lo = 0, hi = 1024u * 1024u * 1024u / kind.granule;  // up to 1 GiB
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo + 1) / 2;
    void* base = nullptr;
    SceUID uid = Alloc(kind, mid * kind.granule, &base);
    if (uid >= 0) {
      sceKernelFreeMemBlock(uid);
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo * kind.granule;
}

// Total memory of this kind when taken in blocks of `chunk` bytes, then in
// granule-sized blocks; frees everything afterwards.
uint64_t TotalAllocatable(const Kind& kind, uint32_t chunk) {
  std::vector<SceUID> uids;
  uint64_t total = 0;
  for (uint32_t size : {chunk, kind.granule}) {
    for (;;) {
      void* base = nullptr;
      SceUID uid = Alloc(kind, size, &base);
      if (uid < 0) {
        break;
      }
      uids.push_back(uid);
      total += size;
      if (uids.size() > 20000) {
        break;
      }
    }
  }
  for (SceUID uid : uids) {
    sceKernelFreeMemBlock(uid);
  }
  return total;
}

volatile uint32_t g_sink = 0;

void Bandwidth(const Kind& kind) {
  constexpr uint32_t kSize = 16 * 1024 * 1024;
  void* base = nullptr;
  SceUID uid = Alloc(kind, kSize, &base);
  if (uid < 0) {
    Log("  %-22s 16 MiB block not available (0x%08X)\n", kind.name, unsigned(uid));
    return;
  }
  auto* p = static_cast<uint32_t*>(base);
  const uint32_t words = kSize / 4;

  uint64_t t0 = NowUs();
  std::memset(base, 0x5A, kSize);
  uint64_t t_memset = NowUs() - t0;

  t0 = NowUs();
  uint32_t sum = 0;
  for (uint32_t i = 0; i < words; i += 4) {
    sum += p[i] + p[i + 1] + p[i + 2] + p[i + 3];
  }
  uint64_t t_read = NowUs() - t0;

  // Dependent reads 4 KiB + 64 bytes apart: one cache line per page, a
  // pattern like scattered guest data.
  t0 = NowUs();
  uint32_t idx = 0;
  constexpr uint32_t kSteps = 1 << 18;
  for (uint32_t i = 0; i < kSteps; ++i) {
    idx = (idx + (4096 + 64) / 4 + (p[idx] & 1)) % words;
  }
  uint64_t t_scatter = NowUs() - t0;
  g_sink = sum + idx;

  const double mb = double(kSize) / (1024.0 * 1024.0);
  Log("  %-22s memset %7.1f MB/s  read %7.1f MB/s  scattered read %6.1f ns\n", kind.name,
      mb / (double(t_memset) / 1e6), mb / (double(t_read) / 1e6),
      double(t_scatter) * 1000.0 / double(kSteps));
  sceKernelFreeMemBlock(uid);
}

}  // namespace

int main() {
  sceIoMkdir("ux0:data/storm2port", 0777);
  sceIoRemove(kLogPath);

  // Screen: yellow while running (CDRAM framebuffer, 1 MiB aligned size).
  uint32_t* fb = nullptr;
  SceUID fb_uid = sceKernelAllocMemBlock("probe_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                         2 * 1024 * 1024, nullptr);
  if (fb_uid >= 0) {
    void* base = nullptr;
    sceKernelGetMemBlockBase(fb_uid, &base);
    fb = static_cast<uint32_t*>(base);
    for (int i = 0; i < 960 * 544; ++i) {
      fb[i] = 0xFF20E0E0u;
    }
    SceDisplayFrameBuf frame{};
    frame.size = sizeof(frame);
    frame.base = fb;
    frame.pitch = 960;
    frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    frame.width = 960;
    frame.height = 544;
    sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
  }

  Log("PS Vita memory probe, variant '%s' (2 MiB of CDRAM used by the screen, 8 MiB newlib heap)\n",
      MEMPROBE_VARIANT);
  LogFree("at start");

  Log("largest single block / total in 16 MiB + granule blocks:\n");
  for (const Kind& kind : kKinds) {
    const uint32_t largest = LargestBlock(kind);
    const uint64_t total = TotalAllocatable(kind, 16 * 1024 * 1024);
    Log("  %-22s largest %7u KiB  total %7llu KiB\n", kind.name, largest / 1024,
        (unsigned long long)(total / 1024));
  }
  LogFree("after");

  Log("CPU access speed (16 MiB block):\n");
  for (const Kind& kind : kKinds) {
    Bandwidth(kind);
  }
  Log("done\n");

  if (fb) {
    for (int i = 0; i < 960 * 544; ++i) {
      fb[i] = 0xFF20C020u;  // green
    }
  }
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
  SceCtrlData pad{};
  for (int i = 0; i < 30; ++i) {
    sceKernelDelayThread(16 * 1000);
  }
  for (;;) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (pad.buttons) {
      break;
    }
    sceKernelDelayThread(16 * 1000);
  }
  sceKernelExitProcess(0);
  return 0;
}

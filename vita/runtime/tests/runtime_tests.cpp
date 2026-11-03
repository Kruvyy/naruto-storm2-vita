// ReXGlue runtime tests for the PS Vita platform layer (B3).
//
// Runs on the console (and in Vita3K) against the real runtime libraries:
// clock, host memory, threads and synchronisation, fibers, files, logging and
// the software-MMU memory system. Results go to
// ux0:data/storm2port/runtime_tests.log (the runtime's own log goes to
// runtime_tests_rex.log next to it). The screen turns yellow while running,
// then green (all passed) or red (failures).
//
// At the end: TRIANGLE runs the no-access probe (reads a page that the
// software MMU marks as no-access; the app is expected to close with an
// error, which proves such pages fault on the console), any other button
// exits.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/memory/mapped_memory.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>
#include <rex/thread.h>
#include <rex/thread/fiber.h>

#include <fast_tls.h>  // vita/runtime/compat

extern "C" void* __real_sceKernelGetTLSAddr(int key);

using namespace std::chrono_literals;

// newlib's malloc heap is one memory block taken at start-up; keep it small so
// sceKernelAllocMemBlock (the software MMU's host blocks) gets the rest.
extern "C" {
unsigned int _newlib_heap_size_user = 32 * 1024 * 1024;
}

namespace {

constexpr const char* kDataDir = "ux0:data/storm2port";
constexpr const char* kTestDir = "ux0:data/storm2port/rt_test";

constexpr const char* kLogPath = "ux0:data/storm2port/runtime_tests.log";
const char* g_test = "";
int g_test_failures = 0;

// Every line opens, appends and closes the file, so the progress is on disk
// even if a later test hangs or the app is killed (Vita3K only writes a file
// out when it is closed).
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

void LogFreeMemory(const char* when) {
  SceKernelFreeMemorySizeInfo mem{};
  mem.size = sizeof(mem);
  if (sceKernelGetFreeMemorySize(&mem) >= 0) {
    Log("  free memory %s: user %d KiB, CDRAM %d KiB, phycont %d KiB\n", when,
        mem.size_user / 1024, mem.size_cdram / 1024, mem.size_phycont / 1024);
  }
}

void Fail(int line, const char* expr) {
  ++g_test_failures;
  Log("  FAIL %s (line %d): %s\n", g_test, line, expr);
}

#define CHECK(c)          \
  do {                    \
    if (!(c)) {           \
      Fail(__LINE__, #c); \
    }                     \
  } while (0)
#define REQUIRE(c)        \
  do {                    \
    if (!(c)) {           \
      Fail(__LINE__, #c); \
      return;             \
    }                     \
  } while (0)

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------
constexpr int kScreenW = 960, kScreenH = 544;
uint32_t* g_fb = nullptr;

void ScreenInit() {
  const unsigned size = (kScreenW * kScreenH * 4 + 0x3FFFF) & ~0x3FFFFu;
  SceUID block =
      sceKernelAllocMemBlock("rt_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, nullptr);
  if (block < 0) {
    return;
  }
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

void Fill(uint32_t color) {
  if (!g_fb) {
    return;
  }
  for (int i = 0; i < kScreenW * kScreenH; ++i) {
    g_fb[i] = color;
  }
}

uint32_t WaitButton() {
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
  SceCtrlData pad{};
  for (int i = 0; i < 30; ++i) {
    sceKernelDelayThread(16 * 1000);
  }
  for (;;) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (pad.buttons != 0) {
      return pad.buttons;
    }
    sceKernelDelayThread(16 * 1000);
  }
}

uint64_t NowUs() {
  return rex::chrono::Clock::QueryHostTickCount() * 1000000ull /
         rex::chrono::Clock::QueryHostTickFrequency();
}

std::unique_ptr<rex::thread::Thread> Spawn(std::function<void()> fn, bool suspended = false) {
  rex::thread::Thread::CreationParameters params;
  params.stack_size = 256 * 1024;
  params.create_suspended = suspended;
  return rex::thread::Thread::Create(params, std::move(fn));
}

bool Join(rex::thread::Thread* t, std::chrono::milliseconds timeout = 5000ms) {
  return rex::thread::Wait(t, false, timeout) == rex::thread::WaitResult::kSuccess;
}

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------
void TestClock() {
  CHECK(rex::chrono::Clock::host_tick_frequency_platform() == 1000000ull);
  const uint64_t t0 = rex::chrono::Clock::host_tick_count_platform();
  rex::thread::Sleep(20ms);
  const uint64_t t1 = rex::chrono::Clock::host_tick_count_platform();
  Log("  sleep 20 ms took %llu us\n", (unsigned long long)(t1 - t0));
  CHECK(t1 - t0 >= 18000);
  CHECK(t1 - t0 < 1000000);
  const uint64_t now = rex::chrono::Clock::QueryHostSystemTime();
  // Between 2024-01-01 and 2100-01-01 in NT time.
  CHECK(now > 133485408000000000ull);
  CHECK(now < 157766400000000000ull);
}

// ---------------------------------------------------------------------------
// Host memory
// ---------------------------------------------------------------------------
void TestHostMemory() {
  using namespace rex::memory;
  const size_t size = 1024 * 1024 + 123;
  auto* p = static_cast<uint8_t*>(
      AllocFixed(nullptr, size, AllocationType::kReserveCommit, PageAccess::kReadWrite));
  REQUIRE(p != nullptr);
  CHECK(p[0] == 0 && p[size - 1] == 0);
  std::memset(p, 0xA5, size);
  CHECK(p[size - 1] == 0xA5);
  size_t length = 0;
  PageAccess access = PageAccess::kNoAccess;
  CHECK(QueryProtect(p + 4096, length, access) && access == PageAccess::kReadWrite);
  CHECK(DeallocFixed(p, size, DeallocationType::kRelease));

  CHECK(AllocFixed(reinterpret_cast<void*>(0x81000000), 4096, AllocationType::kCommit,
                   PageAccess::kReadWrite) == nullptr);

  void* r = AllocFixed(nullptr, 0x10000, AllocationType::kReserve, PageAccess::kNoAccess);
  REQUIRE(r != nullptr);
  CHECK(reinterpret_cast<uintptr_t>(r) < 0x40000000u);
  CHECK(QueryProtect(r, length, access) && access == PageAccess::kNoAccess);
  CHECK(DeallocFixed(r, 0x10000, DeallocationType::kRelease));
  Log("  reservation address 0x%08X\n", unsigned(reinterpret_cast<uintptr_t>(r)));
}

// ---------------------------------------------------------------------------
// Threads and synchronisation
// ---------------------------------------------------------------------------
void TestThreadJoin() {
  std::atomic<int> value{0};
  auto t = Spawn([&] { value = 42; });
  REQUIRE(t != nullptr);
  CHECK(Join(t.get()));
  CHECK(value == 42);
}

void TestThreadCreateSuspended() {
  std::atomic<bool> ran{false};
  auto t = Spawn([&] { ran = true; }, true);
  REQUIRE(t != nullptr);
  rex::thread::Sleep(30ms);
  CHECK(!ran);
  uint32_t previous = 0;
  CHECK(t->Resume(&previous));
  CHECK(previous == 1);
  CHECK(Join(t.get()));
  CHECK(ran);
}

void TestEvents() {
  using rex::thread::WaitResult;
  auto a = rex::thread::Event::CreateAutoResetEvent(false);
  a->Set();
  CHECK(rex::thread::Wait(a.get(), false, 0ms) == WaitResult::kSuccess);
  CHECK(rex::thread::Wait(a.get(), false, 0ms) == WaitResult::kTimeout);
  auto m = rex::thread::Event::CreateManualResetEvent(false);
  m->Set();
  CHECK(rex::thread::Wait(m.get(), false, 0ms) == WaitResult::kSuccess);
  CHECK(rex::thread::Wait(m.get(), false, 0ms) == WaitResult::kSuccess);
  m->Reset();
  CHECK(rex::thread::Wait(m.get(), false, 10ms) == WaitResult::kTimeout);
  // Cross-thread wake-up.
  auto e = rex::thread::Event::CreateAutoResetEvent(false);
  std::atomic<bool> woke{false};
  auto t = Spawn([&] {
    if (rex::thread::Wait(e.get(), false, 2000ms) == WaitResult::kSuccess) {
      woke = true;
    }
  });
  rex::thread::Sleep(20ms);
  e->Set();
  CHECK(Join(t.get()));
  CHECK(woke);
}

void TestSemaphore() {
  using rex::thread::WaitResult;
  auto s = rex::thread::Semaphore::Create(0, 2);
  REQUIRE(s != nullptr);
  int previous = -1;
  CHECK(s->Release(2, &previous) && previous == 0);
  CHECK(!s->Release(1, nullptr));
  CHECK(rex::thread::Wait(s.get(), false, 0ms) == WaitResult::kSuccess);
  CHECK(rex::thread::Wait(s.get(), false, 0ms) == WaitResult::kSuccess);
  CHECK(rex::thread::Wait(s.get(), false, 0ms) == WaitResult::kTimeout);
}

void TestMutant() {
  using rex::thread::WaitResult;
  auto m = rex::thread::Mutant::Create(false);
  CHECK(rex::thread::Wait(m.get(), false, 0ms) == WaitResult::kSuccess);
  CHECK(rex::thread::Wait(m.get(), false, 0ms) == WaitResult::kSuccess);  // recursive
  std::atomic<int> other{0};
  auto t = Spawn([&] {
    other = rex::thread::Wait(m.get(), false, 0ms) == WaitResult::kSuccess ? 1 : 2;
  });
  CHECK(Join(t.get()));
  CHECK(other == 2);  // owned by this thread
  CHECK(m->Release());
  CHECK(m->Release());
  CHECK(!m->Release());
  auto t2 = Spawn([&] {
    other = rex::thread::Wait(m.get(), false, 500ms) == WaitResult::kSuccess ? 3 : 4;
  });
  CHECK(Join(t2.get()));
  CHECK(other == 3);
}

void TestWaitMultiple() {
  using rex::thread::WaitResult;
  auto e1 = rex::thread::Event::CreateManualResetEvent(false);
  auto e2 = rex::thread::Event::CreateManualResetEvent(false);
  rex::thread::WaitHandle* handles[] = {e1.get(), e2.get()};
  e2->Set();
  auto any = rex::thread::WaitAny(handles, 2, false, 100ms);
  CHECK(any.first == WaitResult::kSuccess && any.second == 1);
  CHECK(rex::thread::WaitAll(handles, 2, false, 20ms) == WaitResult::kTimeout);
  e1->Set();
  CHECK(rex::thread::WaitAll(handles, 2, false, 100ms) == WaitResult::kSuccess);
}

// Multi-object waits block on the thread's waker: a signal from another
// thread must wake them (they used to poll every millisecond).
void TestWaitMultipleWake() {
  using rex::thread::WaitResult;
  auto e1 = rex::thread::Event::CreateAutoResetEvent(false);
  auto e2 = rex::thread::Event::CreateAutoResetEvent(false);
  rex::thread::WaitHandle* handles[] = {e1.get(), e2.get()};
  std::atomic<int> index{-1};
  std::atomic<uint64_t> waited{0};
  auto t = Spawn([&] {
    const uint64_t t0 = NowUs();
    auto r = rex::thread::WaitAny(handles, 2, false, 2000ms);
    waited = NowUs() - t0;
    index = r.first == WaitResult::kSuccess ? int(r.second) : -2;
  });
  rex::thread::Sleep(30ms);
  e2->Set();
  CHECK(Join(t.get()));
  Log("  WaitAny woken after %llu us (set at ~30 ms)\n", (unsigned long long)waited.load());
  CHECK(index == 1);
  CHECK(waited < 1000000);
  CHECK(rex::thread::Wait(e2.get(), false, 0ms) == WaitResult::kTimeout);  // consumed
  std::atomic<int> all{-1};
  auto t2 = Spawn([&] { all = int(rex::thread::WaitAll(handles, 2, false, 2000ms)); });
  rex::thread::Sleep(20ms);
  e1->Set();
  rex::thread::Sleep(20ms);
  e2->Set();
  CHECK(Join(t2.get()));
  CHECK(all == int(WaitResult::kSuccess));
}

// An alertable sleep is woken by a queued user callback.
void TestAlertableSleepCallback() {
  std::atomic<int> result{-1};
  std::atomic<bool> ran{false};
  std::atomic<uint64_t> slept{0};
  auto t = Spawn([&] {
    const uint64_t t0 = NowUs();
    result = int(rex::thread::AlertableSleep(2000ms));
    slept = NowUs() - t0;
  });
  rex::thread::Sleep(30ms);
  t->QueueUserCallback([&] { ran = true; });
  CHECK(Join(t.get()));
  Log("  alertable sleep woken after %llu us (callback at ~30 ms)\n",
      (unsigned long long)slept.load());
  CHECK(result == int(rex::thread::SleepResult::kAlerted));
  CHECK(ran);
  CHECK(slept < 1000000);
}

void TestTimer() {
  using rex::thread::WaitResult;
  auto timer = rex::thread::Timer::CreateManualResetTimer();
  const uint64_t t0 = NowUs();
  CHECK(timer->SetOnceAfter(rex::chrono::hundrednanoseconds(200000)));  // 20 ms
  CHECK(rex::thread::Wait(timer.get(), false, 2000ms) == WaitResult::kSuccess);
  const uint64_t dt = NowUs() - t0;
  Log("  20 ms timer fired after %llu us\n", (unsigned long long)dt);
  CHECK(dt >= 15000);
  CHECK(dt < 1000000);
}

void TestTls() {
  auto h = rex::thread::AllocateTlsHandle();
  CHECK(rex::thread::SetTlsValue(h, 0x1234));
  CHECK(rex::thread::GetTlsValue(h) == 0x1234);
  std::atomic<uintptr_t> other{1};
  auto t = Spawn([&] { other = rex::thread::GetTlsValue(h); });
  CHECK(Join(t.get()));
  CHECK(other == 0);
  CHECK(rex::thread::FreeTlsHandle(h));
}

// compat/fast_tls.c: after the probe, sceKernelGetTLSAddr (the wrapper) answers
// from TPIDRURO; it must give the same slot as the real call on any thread,
// and thread_local / errno must keep working.
void TestFastTls() {
  VitaFastTlsReport report{};
  vita_fast_tls_probe(1, &report);
  CHECK(report.consistent);
  CHECK(report.enabled);
  auto same_slots = [] {
    for (const int key : {0, 1, 0x20, 0x89, 0xFF}) {
      if (sceKernelGetTLSAddr(key) != __real_sceKernelGetTLSAddr(key)) {
        return false;
      }
    }
    return true;
  };
  CHECK(same_slots());
  static thread_local int local_value = 0;
  local_value = 7;
  std::atomic<bool> other_slots{false};
  std::atomic<int> other_local{-1};
  std::atomic<int> other_errno{0};
  auto t = Spawn([&] {
    other_slots = same_slots();
    other_local = local_value;  // a new thread sees its own (zero) copy
    errno = ENOENT;
    other_errno = errno;
  });
  CHECK(Join(t.get()));
  CHECK(other_slots);
  CHECK(other_local == 0);
  CHECK(other_errno == ENOENT);
  CHECK(local_value == 7);
  VitaFastTlsStats stats{};
  vita_fast_tls_stats(&stats);
  CHECK(stats.mismatches == 0);
}

void TestThreadIdPriorityAffinity() {
  std::atomic<bool> ids_match{false};
  std::atomic<int> priority{-100};
  std::atomic<uint64_t> affinity{0}, all_cores_affinity{0};
  // vita_threads_all_cores: every thread may run on all three cores and guest
  // affinity requests leave it there.
  REQUIRE(rex::cvar::SetFlagByName("vita_threads_all_cores", "true"));
  auto all = Spawn([&] {
    auto* self = rex::thread::Thread::GetCurrentThread();
    self->set_affinity_mask(0b000100);
    all_cores_affinity = self->affinity_mask();
  });
  CHECK(Join(all.get()));
  REQUIRE(rex::cvar::SetFlagByName("vita_threads_all_cores", "false"));
  CHECK(all_cores_affinity == 0b111111);
  // Default: hardware thread N runs on core N / 2 (vita_hw_thread_cores).
  auto t = Spawn([&] {
    auto* self = rex::thread::Thread::GetCurrentThread();
    ids_match = self->system_id() == rex::thread::current_thread_system_id();
    self->set_priority(rex::thread::ThreadPriority::kAboveNormal);
    priority = self->priority();
    self->set_affinity_mask(0b000100);  // hardware thread 2 -> core 1
    affinity = self->affinity_mask();
  });
  CHECK(Join(t.get()));
  CHECK(ids_match);
  CHECK(priority == rex::thread::ThreadPriority::kAboveNormal);
  CHECK(affinity == 0b001100);
  CHECK(rex::thread::logical_processor_count() == 3);
}

void TestAlertableCallback() {
  using rex::thread::WaitResult;
  auto never = rex::thread::Event::CreateManualResetEvent(false);
  std::atomic<int> result{-1};
  std::atomic<bool> callback_ran{false};
  auto t = Spawn([&] { result = int(rex::thread::Wait(never.get(), true, 2000ms)); });
  rex::thread::Sleep(30ms);
  t->QueueUserCallback([&] { callback_ran = true; });
  CHECK(Join(t.get()));
  CHECK(result == int(WaitResult::kUserCallback));
  CHECK(callback_ran);
}

void TestDeferredSuspend() {
  std::atomic<uint32_t> counter{0};
  std::atomic<bool> stop{false};
  auto t = Spawn([&] {
    while (!stop) {
      ++counter;
      rex::thread::Sleep(1ms);
    }
  });
  rex::thread::Sleep(50ms);
  CHECK(t->Suspend());
  rex::thread::Sleep(30ms);
  const uint32_t c1 = counter;
  rex::thread::Sleep(100ms);
  const uint32_t c2 = counter;
  Log("  counter while suspended: %u -> %u\n", c1, c2);
  CHECK(c1 == c2);
  CHECK(t->Resume());
  rex::thread::Sleep(50ms);
  CHECK(counter > c2);
  stop = true;
  CHECK(Join(t.get()));
}

void TestSelfSuspend() {
  std::atomic<bool> resumed{false};
  std::atomic<bool> started{false};
  auto t = Spawn([&] {
    started = true;
    rex::thread::Thread::GetCurrentThread()->Suspend();
    resumed = true;
  });
  rex::thread::Sleep(50ms);
  CHECK(started);
  CHECK(!resumed);
  CHECK(t->Resume());
  CHECK(Join(t.get()));
  CHECK(resumed);
}

void TestDeferredTerminate() {
  std::atomic<bool> after{false};
  auto t = Spawn([&] {
    for (;;) {
      rex::thread::Sleep(1ms);
    }
    after = true;
  });
  rex::thread::Sleep(30ms);
  t->Terminate(7);
  CHECK(Join(t.get(), 2000ms));
  CHECK(!after);
}

// ---------------------------------------------------------------------------
// Fibers
// ---------------------------------------------------------------------------
struct FiberState {
  rex::thread::Fiber* main = nullptr;
  rex::thread::Fiber* f1 = nullptr;
  rex::thread::Fiber* f2 = nullptr;
  std::string seq;
};

void FiberEntry1(void* arg) {
  auto* s = static_cast<FiberState*>(arg);
  s->seq += "1";
  rex::thread::Fiber::SwitchTo(s->f2);
  s->seq += "3";
  rex::thread::Fiber::SwitchTo(s->main);
  for (;;) {
  }
}

void FiberEntry2(void* arg) {
  auto* s = static_cast<FiberState*>(arg);
  s->seq += "2";
  rex::thread::Fiber::SwitchTo(s->f1);
  for (;;) {
  }
}

void TestFibers() {
  FiberState s;
  auto t = Spawn([&] {
    s.main = rex::thread::Fiber::ConvertCurrentThread();
    s.f1 = rex::thread::Fiber::Create(64 * 1024, FiberEntry1, &s);
    s.f2 = rex::thread::Fiber::Create(64 * 1024, FiberEntry2, &s);
    if (!s.main || !s.f1 || !s.f2) {
      return;
    }
    s.seq += "0";
    rex::thread::Fiber::SwitchTo(s.f1);
    s.seq += "4";
    s.f1->Destroy();
    s.f2->Destroy();
    s.main->Destroy();
  });
  CHECK(Join(t.get()));
  Log("  fiber order: %s\n", s.seq.c_str());
  CHECK(s.seq == "01234");
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
void TestFiles() {
  sceIoMkdir(kTestDir, 0777);
  const std::filesystem::path file = std::string(kTestDir) + "/a.bin";
  CHECK(rex::filesystem::CreateEmptyFile(file));
  auto h = rex::filesystem::FileHandle::OpenExisting(
      file, rex::filesystem::FileAccess::kGenericRead | rex::filesystem::FileAccess::kGenericWrite);
  REQUIRE(h != nullptr);
  std::vector<uint8_t> data(1000);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = uint8_t(i * 7);
  }
  size_t n = 0;
  CHECK(h->Write(0, data.data(), data.size(), &n) && n == data.size());
  std::vector<uint8_t> back(1000);
  CHECK(h->Read(0, back.data(), back.size(), &n) && n == back.size() && back == data);
  CHECK(h->SetLength(500));
  h.reset();
  rex::filesystem::FileInfo info{};
  CHECK(rex::filesystem::GetInfo(file, &info));
  CHECK(info.type == rex::filesystem::FileInfo::Type::kFile && info.total_size == 500);
  CHECK(info.write_timestamp > 133485408000000000ull);
  bool listed = false;
  for (const auto& e : rex::filesystem::ListFiles(kTestDir)) {
    listed |= e.name == "a.bin";
  }
  CHECK(listed);

  // "Mapped" file: modify, close, read back.
  {
    auto map = rex::memory::MappedMemory::Open(file, rex::memory::MappedMemory::Mode::kReadWrite);
    REQUIRE(map != nullptr);
    CHECK(map->size() == 500);
    map->data()[10] = 0xEE;
    map->Close();
  }
  {
    auto map = rex::memory::MappedMemory::Open(file, rex::memory::MappedMemory::Mode::kRead);
    REQUIRE(map != nullptr);
    CHECK(map->data()[10] == 0xEE && map->data()[11] == uint8_t(11 * 7));
  }
  CHECK(sceIoRemove(file.string().c_str()) >= 0);
}

void TestStdFilesystem() {
  // libstdc++'s std::filesystem over newlib, with Vita device paths: the
  // runtime uses it for directory creation and enumeration.
  std::error_code ec;
  const std::filesystem::path sub = std::string(kTestDir) + "/sub/deeper";
  CHECK(std::filesystem::exists(kDataDir, ec));
  CHECK(std::filesystem::create_directories(sub, ec) || std::filesystem::is_directory(sub, ec));
  Log("  create_directories: %s\n", ec.message().c_str());
  CHECK(std::filesystem::is_directory(sub, ec));
  size_t entries = 0;
  for (auto it = std::filesystem::directory_iterator(std::string(kTestDir) + "/sub", ec);
       !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    ++entries;
  }
  CHECK(entries == 1);
  CHECK(std::filesystem::remove_all(std::string(kTestDir) + "/sub", ec) >= 2);
  CHECK(!std::filesystem::exists(sub, ec));
}

// ---------------------------------------------------------------------------
// Software-MMU memory system (same cases as rexglue/tests/soft_mmu)
// ---------------------------------------------------------------------------
using namespace rex::memory;
constexpr uint32_t kRW = kMemoryProtectRead | kMemoryProtectWrite;
constexpr uint32_t kReserveCommit = kMemoryAllocationReserve | kMemoryAllocationCommit;

Memory* g_memory = nullptr;

uint64_t HostBacked() {
  GuestMemoryStats stats{};
  g_memory->GetGuestMemoryStats(stats);
  return stats.host_backed;
}

void TestSoftMmuInit() {
  g_memory = new Memory();
  REQUIRE(g_memory->Initialize());
  Log("  host backed after init: %llu bytes\n", (unsigned long long)HostBacked());
}

void TestSoftMmuVirtual() {
  REQUIRE(g_memory);
  auto& m = *g_memory;
  uint32_t a = 0;
  REQUIRE(m.LookupHeap(0x10000000)->Alloc(0x5000, 0x1000, kReserveCommit, kRW, false, &a));
  uint8_t* host = m.TranslateVirtual(a);
  for (uint32_t off = 0; off < 0x5000; off += 0x1000) {
    CHECK(m.TranslateVirtual(a + off) == host + off);
    CHECK(GuestPtr(m.virtual_membase(), a + off + 3) == host + off + 3);
  }
  std::memset(host, 0x5A, 0x5000);
  CHECK(*m.TranslateVirtual(a + 0x4FFF) == 0x5A);
  CHECK(m.HostToGuestVirtual(host + 0x2345) == a + 0x2345);
  m.LookupHeap(a)->Release(a);
}

void TestSoftMmuViews() {
  REQUIRE(g_memory);
  auto& m = *g_memory;
  uint32_t a0 = 0;
  REQUIRE(m.LookupHeap(0xA0000000)->Alloc(0x20000, 0x10000, kReserveCommit, kRW, false, &a0));
  const uint32_t phys = a0 - 0xA0000000;
  uint8_t* host = m.TranslatePhysical(phys);
  CHECK(m.TranslateVirtual(a0) == host);
  CHECK(m.TranslateVirtual(0xC0000000 + phys) == host);
  CHECK(m.TranslateVirtual(0xE0000000 + phys - 0x1000) == host);
  CHECK(m.HostToGuestVirtual(host + 0x10) == a0 + 0x10);
  m.LookupHeap(a0)->Release(a0);

  uint32_t e0 = 0;
  REQUIRE(m.LookupHeap(0xE0000000)->Alloc(0x3000, 0x1000, kReserveCommit, kRW, false, &e0));
  const uint32_t p2 = e0 - 0xE0000000 + 0x1000;
  CHECK(m.TranslateVirtual(e0) == m.TranslatePhysical(p2));
  CHECK(m.TranslateVirtual(0xA0000000 + p2) == m.TranslatePhysical(p2));
  m.LookupHeap(e0)->Release(e0);
}

void TestSoftMmuAliasProtectRelease() {
  REQUIRE(g_memory);
  auto& m = *g_memory;
  REQUIRE(m.LookupHeap(0x8F000000)->AllocFixed(0x8F000000, 0x10000, 0x10000, kReserveCommit, kRW));
  CHECK(m.TranslateVirtual(0x9F000000) == m.TranslateVirtual(0x8F000000));
  m.LookupHeap(0x8F000000)->Release(0x8F000000);
  CHECK(m.TranslateVirtual(0x9F000000) == m.TranslateVirtual(0x8F000000));

  auto* heap = m.LookupHeap(0x10000000);
  uint32_t a = 0;
  REQUIRE(heap->Alloc(0x2000, 0x1000, kReserveCommit, kRW, false, &a));
  uint8_t* host = m.TranslateVirtual(a);
  uint8_t* unmapped = m.TranslateVirtual(0x2F000000);
  CHECK(heap->Protect(a + 0x1000, 0x1000, kMemoryProtectNoAccess));
  CHECK(m.TranslateVirtual(a + 0x1000) == unmapped);
  CHECK(heap->Protect(a + 0x1000, 0x1000, kRW));
  CHECK(m.TranslateVirtual(a + 0x1000) == host + 0x1000);
  heap->Release(a);
  uint8_t* released = m.TranslateVirtual(a);
  CHECK(released != unmapped && released[0] == 0);
}

void TestSoftMmuReserveCap() {
  REQUIRE(g_memory);
  auto& m = *g_memory;
  auto* heap = m.LookupHeap(0x40000000);
  uint32_t a = 0;
  const uint64_t before = HostBacked();
  REQUIRE(heap->Alloc(0x100000, 0x10000, kMemoryAllocationReserve, kMemoryProtectNoAccess, false,
                      &a));
  CHECK(HostBacked() == before);
  REQUIRE(heap->AllocFixed(a + 0x40000, 0x10000, 0x10000, kMemoryAllocationCommit, kRW));
  CHECK(HostBacked() == before + 0x100000);
  heap->Release(a);
  CHECK(HostBacked() == before);

  REQUIRE(rex::cvar::SetFlagByName("soft_mmu_caps", "0x7C000000=1"));
  REQUIRE(heap->AllocFixed(0x7C000000, 0x400000, 0x10000, kReserveCommit, kRW));
  CHECK(HostBacked() == before + 0x100000 + 0x1000);
  uint8_t* guard = m.TranslateVirtual(0x7C100000);
  CHECK(m.TranslateVirtual(0x7C3FF000) == guard);
  m.SoftCheckGuards();
  heap->Release(0x7C000000);
  REQUIRE(rex::cvar::SetFlagByName("soft_mmu_caps", ""));
  CHECK(HostBacked() == before);
}

void TestSoftMmuLargeRegion() {
  // A region the size of Storm 2's big physical pool, limited to 184 MiB:
  // the Vita must provide 184 MiB in one block.
  REQUIRE(g_memory);
  auto& m = *g_memory;
  REQUIRE(rex::cvar::SetFlagByName("soft_mmu_caps", "0xAB000000=184"));
  LogFreeMemory("before");
  Log("  allocating the 272 MiB region...\n");
  const uint64_t before = HostBacked();
  const uint64_t t0 = NowUs();
  bool ok = m.LookupHeap(0xA0000000)->AllocFixed(0xAB000000, 272u << 20, 0x10000, kReserveCommit,
                                                 kRW);
  const uint64_t dt = NowUs() - t0;
  Log("  272 MiB physical region capped at 184 MiB: %s, %llu MiB backed, %llu ms\n",
      ok ? "ok" : "FAILED", (unsigned long long)((HostBacked() - before) >> 20),
      (unsigned long long)(dt / 1000));
  CHECK(ok);
  if (ok) {
    LogFreeMemory("with the region");
    uint8_t* p = m.TranslateVirtual(0xAB000000);
    p[0] = 1;
    p[(184u << 20) - 1] = 2;
    CHECK(m.TranslateVirtual(0xAB000000 + (184u << 20) - 1) == p + (184u << 20) - 1);
    m.LookupHeap(0xAB000000)->Release(0xAB000000);
    Log("  released\n");
  }
  REQUIRE(rex::cvar::SetFlagByName("soft_mmu_caps", ""));
}

void TestSoftMmuFunctionTable() {
  REQUIRE(g_memory);
  auto* fake = reinterpret_cast<PPCFunc*>(uintptr_t(0x1234560));
  REQUIRE(g_memory->InitializeFunctionTable(0x82100000, 0x1000, 0x82000000, 0x200000));
  CHECK(g_memory->SetFunction(0x82100008, fake));
  CHECK(g_memory->soft_function_table()[2] == fake);
}

void TestLogging() {
  REXLOG_INFO("runtime_tests: log line from the ReXGlue logger");
  rex::FlushLogging();
  SceIoStat st{};
  const std::string path = std::string(kDataDir) + "/runtime_tests_rex.log";
  CHECK(sceIoGetstat(path.c_str(), &st) >= 0 && st.st_size > 0);
}

// ---------------------------------------------------------------------------
// Scheduling benchmark (always passes; prints numbers). How the system (or the
// emulator) runs threads given a core mask: parallel speed-up, the cost of
// atomics and system calls when threads run at the same time, and whether a
// thread that spins starves another of the same priority on its core.
// ---------------------------------------------------------------------------
namespace sched_bench {

enum Kind { kCompute, kAtomicOwn, kAtomicShared, kSyscall, kKinds };
const char* const kKindNames[kKinds] = {"compute", "atomic (own line)", "atomic (shared)",
                                        "system call"};
const uint32_t kIterations[kKinds] = {20000000, 2000000, 2000000, 200000};

struct alignas(64) Slot {
  std::atomic<uint32_t> value{0};
};
Slot g_slots[4];
volatile uint32_t g_sink;

struct Job {
  Kind kind;
  int index;
  uint64_t elapsed_us;
};

int Worker(SceSize, void* argp) {
  Job* job = *static_cast<Job**>(argp);
  const uint32_t n = kIterations[job->kind];
  const uint64_t start = sceKernelGetProcessTimeWide();
  switch (job->kind) {
    case kCompute: {
      uint32_t x = 1 + job->index;
      for (uint32_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
      }
      g_sink = x;
      break;
    }
    case kAtomicOwn:
      for (uint32_t i = 0; i < n; ++i) {
        g_slots[job->index].value.fetch_add(1, std::memory_order_relaxed);
      }
      break;
    case kAtomicShared:
      for (uint32_t i = 0; i < n; ++i) {
        g_slots[3].value.fetch_add(1, std::memory_order_relaxed);
      }
      break;
    default:
      for (uint32_t i = 0; i < n; ++i) {
        g_sink = uint32_t(sceKernelGetProcessTimeWide());
      }
      break;
  }
  job->elapsed_us = sceKernelGetProcessTimeWide() - start;
  return 0;
}

// Runs one job per mask at the same time; returns the wall time in us.
uint64_t Run(Kind kind, const std::vector<int>& masks) {
  std::vector<Job> jobs(masks.size());
  std::vector<SceUID> ids;
  for (size_t i = 0; i < masks.size(); ++i) {
    jobs[i] = Job{kind, int(i), 0};
    const SceUID id = sceKernelCreateThread("sched_bench", Worker, 0x10000100, 0x4000, 0,
                                            masks[i], nullptr);
    if (id < 0) {
      return 0;
    }
    ids.push_back(id);
  }
  const uint64_t start = sceKernelGetProcessTimeWide();
  for (size_t i = 0; i < ids.size(); ++i) {
    Job* ptr = &jobs[i];
    sceKernelStartThread(ids[i], sizeof(ptr), &ptr);
  }
  for (SceUID id : ids) {
    sceKernelWaitThreadEnd(id, nullptr, nullptr);
    sceKernelDeleteThread(id);
  }
  return sceKernelGetProcessTimeWide() - start;
}

// Thread A spins on a flag that thread B sets, both on the same core with the
// same priority. Returns how long B took to run (us), capped at the spin limit.
std::atomic<bool> g_flag{false};
std::atomic<uint64_t> g_b_ran_at{0};
int SpinnerA(SceSize, void* argp) {
  const bool yield = *static_cast<bool*>(argp);
  const uint64_t start = sceKernelGetProcessTimeWide();
  while (!g_flag.load(std::memory_order_acquire) &&
         sceKernelGetProcessTimeWide() - start < 300000) {
    if (yield) {
      sceKernelDelayThread(0);
    }
  }
  return 0;
}
int SetterB(SceSize, void*) {
  g_b_ran_at = sceKernelGetProcessTimeWide();
  g_flag.store(true, std::memory_order_release);
  return 0;
}
uint64_t Starvation(int mask, bool yield) {
  g_flag = false;
  g_b_ran_at = 0;
  const SceUID a = sceKernelCreateThread("spin_a", SpinnerA, 0x10000100, 0x4000, 0, mask, nullptr);
  const SceUID b = sceKernelCreateThread("set_b", SetterB, 0x10000100, 0x4000, 0, mask, nullptr);
  if (a < 0 || b < 0) {
    return 0;
  }
  sceKernelStartThread(a, sizeof(yield), &yield);
  sceKernelDelayThread(2000);  // A is spinning now
  const uint64_t start = sceKernelGetProcessTimeWide();
  sceKernelStartThread(b, 0, nullptr);
  sceKernelWaitThreadEnd(a, nullptr, nullptr);
  sceKernelWaitThreadEnd(b, nullptr, nullptr);
  sceKernelDeleteThread(a);
  sceKernelDeleteThread(b);
  return g_b_ran_at ? g_b_ran_at - start : 0;
}

}  // namespace sched_bench

void TestSchedBench() {
  using namespace sched_bench;
  constexpr int kDefault = 0, kAll = SCE_KERNEL_CPU_MASK_USER_ALL;
  constexpr int k0 = SCE_KERNEL_CPU_MASK_USER_0, k1 = SCE_KERNEL_CPU_MASK_USER_1,
                k2 = SCE_KERNEL_CPU_MASK_USER_2;
  struct Layout {
    const char* name;
    std::vector<int> masks;
  };
  const Layout layouts[] = {
      {"1 thread, core 1", {k1}},
      {"3 threads, default mask", {kDefault, kDefault, kDefault}},
      {"3 threads, cores 0/1/2", {k0, k1, k2}},
      {"3 threads, all cores", {kAll, kAll, kAll}},
      {"2 threads, both core 1", {k1, k1}},
  };
  for (int kind = 0; kind < kKinds; ++kind) {
    std::string line = std::string("  ") + kKindNames[kind] + ":";
    for (const Layout& layout : layouts) {
      const uint64_t us = Run(Kind(kind), layout.masks);
      line += fmt::format(" [{}] {:.1f} ms;", layout.name, double(us) / 1000.0);
    }
    Log("%s\n", line.c_str());
  }
  for (const bool yield : {false, true}) {
    for (const int mask : {k1, kDefault}) {
      const uint64_t us = Starvation(mask, yield);
      Log("  spinner%s on %s: the other thread ran after %s\n", yield ? " with DelayThread(0)" : "",
          mask ? "core 1" : "the default mask",
          us ? fmt::format("{:.1f} ms", double(us) / 1000.0).c_str() : "the spinner gave up (300 ms)");
    }
  }
  CHECK(true);
}

struct TestEntry {
  const char* name;
  void (*fn)();
};

const TestEntry kTests[] = {
    {"clock", TestClock},
    {"host_memory", TestHostMemory},
    {"thread_join", TestThreadJoin},
    {"thread_create_suspended", TestThreadCreateSuspended},
    {"events", TestEvents},
    {"semaphore", TestSemaphore},
    {"mutant", TestMutant},
    {"wait_multiple", TestWaitMultiple},
    {"wait_multiple_wake", TestWaitMultipleWake},
    {"alertable_sleep_callback", TestAlertableSleepCallback},
    {"timer", TestTimer},
    {"tls", TestTls},
    {"fast_tls", TestFastTls},
    {"thread_id_priority_affinity", TestThreadIdPriorityAffinity},
    {"alertable_callback", TestAlertableCallback},
    {"deferred_suspend", TestDeferredSuspend},
    {"self_suspend", TestSelfSuspend},
    {"deferred_terminate", TestDeferredTerminate},
    {"fibers", TestFibers},
    {"files", TestFiles},
    {"std_filesystem", TestStdFilesystem},
    {"soft_mmu_init", TestSoftMmuInit},
    {"soft_mmu_virtual", TestSoftMmuVirtual},
    {"soft_mmu_views", TestSoftMmuViews},
    {"soft_mmu_alias_protect_release", TestSoftMmuAliasProtectRelease},
    {"soft_mmu_reserve_cap", TestSoftMmuReserveCap},
    {"soft_mmu_large_region", TestSoftMmuLargeRegion},
    {"soft_mmu_function_table", TestSoftMmuFunctionTable},
    {"logging", TestLogging},
    {"sched_bench", TestSchedBench},
};

}  // namespace

int main() {
  ScreenInit();
  Fill(0xFF20E0E0u);  // yellow: running
  sceIoMkdir(kDataDir, 0777);
  sceIoRemove(kLogPath);
  sceIoRemove("ux0:data/storm2port/runtime_tests_rex.log");
  Log("ReXGlue runtime tests (PS Vita platform layer)\n");
  LogFreeMemory("at start");
  {
    rex::LogConfig config;
    const std::string rex_log = std::string(kDataDir) + "/runtime_tests_rex.log";
    config.log_file = rex_log.c_str();
    rex::InitLogging(config);
  }

  int failed_tests = 0;
  const unsigned count = sizeof(kTests) / sizeof(kTests[0]);
  for (unsigned i = 0; i < count; ++i) {
    g_test = kTests[i].name;
    g_test_failures = 0;
    Log("> %s\n", g_test);
    const uint64_t t0 = NowUs();
    kTests[i].fn();
    const uint64_t dt = NowUs() - t0;
    Log("%-32s %s (%llu ms)\n", g_test, g_test_failures ? "FAILED" : "ok",
        (unsigned long long)(dt / 1000));
    failed_tests += g_test_failures ? 1 : 0;
  }
  LogFreeMemory("at the end");
  Log("result: %u tests, %d failed\n", count, failed_tests);
  Log("TRIANGLE = no-access probe (the app should close with an error); other button = exit\n");
  rex::FlushLogging();
  Fill(failed_tests ? 0xFF2020E0u : 0xFF20C020u);  // red / green

  const uint32_t buttons = WaitButton();
  if (buttons & SCE_CTRL_TRIANGLE) {
    Log("probe: reading a no-access guest page through the software MMU\n");
    volatile uint8_t* p = g_memory ? g_memory->TranslateVirtual(0x2F000000) : nullptr;
    const uint8_t v = p ? *p : 0;
    Log("PROBE DID NOT FAULT: read 0x%02X from 0x%08X\n", v,
        unsigned(reinterpret_cast<uintptr_t>(p)));
    WaitButton();
  }
  sceKernelExitProcess(0);
  return 0;
}

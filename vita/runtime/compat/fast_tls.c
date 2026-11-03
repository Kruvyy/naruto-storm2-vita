/* Faster sceKernelGetTLSAddr, plus counters of frequent system calls.
 *
 * newlib's __getreent() calls sceKernelGetTLSAddr(0x89) on every malloc, free,
 * errno access and pthread_getspecific; GCC's thread_local on this target is
 * emutls, which ends in pthread_getspecific too. On the console the call is a
 * short library function, but Vita3K emulates it on the host, so every C++
 * thread_local access in the runtime costs an emulated system call there.
 *
 * The TLS slots are at a fixed offset from the thread's TPIDRURO register
 * (user read-only, readable with one instruction). vita_fast_tls_probe() measures
 * that offset against the real call on the calling thread and on a new thread,
 * and only turns the fast path on when every probed key agrees on both. The
 * linker routes calls to the wrapper (-Wl,--wrap=sceKernelGetTLSAddr in
 * vita/runtime/CMakeLists.txt). */

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/lw_mutex.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fast_tls.h"

void* __real_sceKernelGetTLSAddr(int key);

static volatile int g_mode;          /* 1: fast path on */
static volatile intptr_t g_delta;    /* slot k is at TPIDRURO + g_delta + 4 * k */
static volatile uint32_t g_check_every;
static uint32_t g_check_count;
static volatile uint32_t g_mismatches;
static uint64_t g_calls;

static inline uintptr_t ReadTpidruro(void) {
  uintptr_t value;
  __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(value));
  return value;
}

/* Lossy (no atomic read-modify-write) but cheap: statistics only. */
static inline void Count(uint64_t* counter) {
  __atomic_store_n(counter, __atomic_load_n(counter, __ATOMIC_RELAXED) + 1, __ATOMIC_RELAXED);
}

void* __wrap_sceKernelGetTLSAddr(int key) {
  Count(&g_calls);
  if (g_mode > 0 && (unsigned)key < 0x100) {
    void* fast = (void*)(ReadTpidruro() + g_delta + 4 * key);
    const uint32_t every = g_check_every;
    if (every) {
      const uint32_t n = __atomic_load_n(&g_check_count, __ATOMIC_RELAXED) + 1;
      __atomic_store_n(&g_check_count, n, __ATOMIC_RELAXED);
      if (n % every == 0) {
        void* real = __real_sceKernelGetTLSAddr(key);
        if (real != fast) {
          g_mode = -1;
          g_mismatches = g_mismatches + 1;
          return real;
        }
      }
    }
    return fast;
  }
  return __real_sceKernelGetTLSAddr(key);
}

static const int kProbeKeys[] = {0, 1, 0x40, 0x89, 0xFF};
#define PROBE_KEYS (int)(sizeof(kProbeKeys) / sizeof(kProbeKeys[0]))

typedef struct {
  uintptr_t tp;
  intptr_t delta[PROBE_KEYS];
} ProbeResult;

static void ProbeHere(ProbeResult* out) {
  out->tp = ReadTpidruro();
  for (int i = 0; i < PROBE_KEYS; ++i) {
    const uintptr_t real = (uintptr_t)__real_sceKernelGetTLSAddr(kProbeKeys[i]);
    out->delta[i] = (intptr_t)(real - out->tp) - 4 * kProbeKeys[i];
  }
}

static int ProbeThread(SceSize args, void* argp) {
  (void)args;
  ProbeHere(*(ProbeResult**)argp);
  return 0;
}

int vita_fast_tls_probe(int enable, VitaFastTlsReport* report) {
  memset(report, 0, sizeof(*report));
  ProbeResult here, other;
  memset(&other, 0, sizeof(other));
  ProbeHere(&here);
  const SceUID thread = sceKernelCreateThread("fast_tls_probe", ProbeThread, 0x10000100, 0x4000,
                                              0, 0, NULL);
  if (thread >= 0) {
    ProbeResult* ptr = &other;
    sceKernelStartThread(thread, sizeof(ptr), &ptr);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    sceKernelDeleteThread(thread);
  }
  report->tp_main = here.tp;
  report->tp_other = other.tp;
  report->delta = here.delta[0];
  int consistent = thread >= 0 && here.tp != 0 && other.tp != 0 && here.tp != other.tp;
  for (int i = 0; i < PROBE_KEYS; ++i) {
    if (here.delta[i] != here.delta[0] || other.delta[i] != here.delta[0]) {
      consistent = 0;
    }
  }
  report->consistent = consistent;

  /* Cost of one call: real, then the fast path (same code the wrapper runs). */
  enum { kCalls = 20000 };
  uint64_t start = sceKernelGetProcessTimeWide();
  for (int i = 0; i < kCalls; ++i) {
    __asm__ volatile("" ::"r"(__real_sceKernelGetTLSAddr(0x89)));
  }
  report->real_ns = (uint32_t)((sceKernelGetProcessTimeWide() - start) * 1000 / kCalls);
  start = sceKernelGetProcessTimeWide();
  for (int i = 0; i < kCalls; ++i) {
    __asm__ volatile("" ::"r"(ReadTpidruro() + here.delta[0] + 4 * 0x89));
  }
  report->fast_ns = (uint32_t)((sceKernelGetProcessTimeWide() - start) * 1000 / kCalls);

  if (enable && consistent) {
    g_delta = here.delta[0];
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    g_mode = 1;
  }
  report->enabled = g_mode > 0;
  return report->enabled;
}

void vita_fast_tls_set_check(uint32_t every) {
  g_check_every = every;
}

void vita_fast_tls_stats(VitaFastTlsStats* out) {
  out->calls = __atomic_load_n(&g_calls, __ATOMIC_RELAXED);
  out->mismatches = g_mismatches;
  out->enabled = g_mode > 0;
}

/* Counters of other frequent system calls (each one is emulated on the host
 * in Vita3K). The wrappers only count; the names are in kSyscallNames. */
enum {
  kTimeWide,
  kTimeLow,
  kThreadId,
  kLockLw,
  kTryLockLw,
  kUnlockLw,
  kWaitSema,
  kSignalSema,
  kPollEventFlag,
  kDelayThread,
  kGettimeofday,
  kMutexLock,
  kMalloc,
  kFree,
  kGlobalLock,
  kSyscallCount
};
static const char* const kSyscallNames[kSyscallCount] = {
    "GetProcessTimeWide", "GetProcessTimeLow", "GetThreadId",       "LockLwMutex",
    "TryLockLwMutex",     "UnlockLwMutex",     "WaitSema",          "SignalSema",
    "PollEventFlag",      "DelayThread",       "LibcGettimeofday",  "pthread_mutex_lock",
    "malloc",             "free",              "global lock"};
static uint64_t g_syscalls[kSyscallCount];

/* Call sites (return addresses) per counter: a small lossy table. */
#define kSites 256
typedef struct {
  uintptr_t addr;
  uint32_t count;
} Site;
static Site g_sites[kSyscallCount][kSites];

static void CountSite(int kind, uintptr_t ra) {
  const uint32_t h = (uint32_t)(ra * 2654435761u) >> 24;
  for (uint32_t i = 0; i < 16; ++i) {  /* a full table drops new sites */
    Site* s = &g_sites[kind][(h + i) & (kSites - 1)];
    uintptr_t a = __atomic_load_n(&s->addr, __ATOMIC_RELAXED);
    if (a == 0) {
      uintptr_t expected = 0;
      if (__atomic_compare_exchange_n(&s->addr, &expected, ra, 0, __ATOMIC_RELAXED,
                                      __ATOMIC_RELAXED)) {
        a = ra;
      } else {
        a = expected;
      }
    }
    if (a == ra) {
      __atomic_store_n(&s->count, __atomic_load_n(&s->count, __ATOMIC_RELAXED) + 1,
                       __ATOMIC_RELAXED);
      return;
    }
  }
}

#define COUNT_SITE(kind)            \
  do {                              \
    Count(&g_syscalls[kind]);       \
    CountSite(kind, (uintptr_t)__builtin_return_address(0)); \
  } while (0)

int vita_syscall_top_sites(int kind, uintptr_t* addrs, uint32_t* counts, int max) {
  if (kind < 0 || kind >= kSyscallCount) {
    return 0;
  }
  Site copy[kSites];
  for (int i = 0; i < kSites; ++i) {
    copy[i].addr = __atomic_load_n(&g_sites[kind][i].addr, __ATOMIC_RELAXED);
    copy[i].count = __atomic_load_n(&g_sites[kind][i].count, __ATOMIC_RELAXED);
  }
  int n = 0;
  for (; n < max; ++n) {
    int best = -1;
    for (int i = 0; i < kSites; ++i) {
      if (copy[i].addr && (best < 0 || copy[i].count > copy[best].count)) {
        best = i;
      }
    }
    if (best < 0) {
      break;
    }
    addrs[n] = copy[best].addr;
    counts[n] = copy[best].count;
    copy[best].addr = 0;
  }
  return n;
}

SceUInt64 __real_sceKernelGetProcessTimeWide(void);
SceUInt64 __wrap_sceKernelGetProcessTimeWide(void) {
  COUNT_SITE(kTimeWide);
  return __real_sceKernelGetProcessTimeWide();
}
SceUInt32 __real_sceKernelGetProcessTimeLow(void);
SceUInt32 __wrap_sceKernelGetProcessTimeLow(void) {
  Count(&g_syscalls[kTimeLow]);
  return __real_sceKernelGetProcessTimeLow();
}
int __real_sceKernelGetThreadId(void);
int __wrap_sceKernelGetThreadId(void) {
  Count(&g_syscalls[kThreadId]);
  return __real_sceKernelGetThreadId();
}
int __real_sceKernelLockLwMutex(SceKernelLwMutexWork* work, int count, unsigned int* timeout);
int __wrap_sceKernelLockLwMutex(SceKernelLwMutexWork* work, int count, unsigned int* timeout) {
  COUNT_SITE(kLockLw);
  return __real_sceKernelLockLwMutex(work, count, timeout);
}
int __real_sceKernelTryLockLwMutex(SceKernelLwMutexWork* work, int count);
int __wrap_sceKernelTryLockLwMutex(SceKernelLwMutexWork* work, int count) {
  Count(&g_syscalls[kTryLockLw]);
  return __real_sceKernelTryLockLwMutex(work, count);
}
int __real_sceKernelUnlockLwMutex(SceKernelLwMutexWork* work, int count);
int __wrap_sceKernelUnlockLwMutex(SceKernelLwMutexWork* work, int count) {
  Count(&g_syscalls[kUnlockLw]);
  return __real_sceKernelUnlockLwMutex(work, count);
}
int __real_sceKernelWaitSema(SceUID sema, int signal, SceUInt* timeout);
int __wrap_sceKernelWaitSema(SceUID sema, int signal, SceUInt* timeout) {
  COUNT_SITE(kWaitSema);
  return __real_sceKernelWaitSema(sema, signal, timeout);
}
int __real_sceKernelSignalSema(SceUID sema, int signal);
int __wrap_sceKernelSignalSema(SceUID sema, int signal) {
  Count(&g_syscalls[kSignalSema]);
  return __real_sceKernelSignalSema(sema, signal);
}
int __real_sceKernelPollEventFlag(int evid, unsigned int bits, unsigned int wait,
                                  unsigned int* out_bits);
int __wrap_sceKernelPollEventFlag(int evid, unsigned int bits, unsigned int wait,
                                  unsigned int* out_bits) {
  Count(&g_syscalls[kPollEventFlag]);
  return __real_sceKernelPollEventFlag(evid, bits, wait, out_bits);
}
int __real_sceKernelDelayThread(SceUInt delay);
int __wrap_sceKernelDelayThread(SceUInt delay) {
  COUNT_SITE(kDelayThread);
  return __real_sceKernelDelayThread(delay);
}
int __real_sceKernelLibcGettimeofday(SceKernelTimeval* tv, SceKernelTimezone* tz);
int __wrap_sceKernelLibcGettimeofday(SceKernelTimeval* tv, SceKernelTimezone* tz) {
  COUNT_SITE(kGettimeofday);
  return __real_sceKernelLibcGettimeofday(tv, tz);
}
int __real_pthread_mutex_lock(pthread_mutex_t* mutex);
int __wrap_pthread_mutex_lock(pthread_mutex_t* mutex) {
  COUNT_SITE(kMutexLock);
  return __real_pthread_mutex_lock(mutex);
}
void* __real_malloc(size_t size);
void* __wrap_malloc(size_t size) {
  COUNT_SITE(kMalloc);
  return __real_malloc(size);
}
void __real_free(void* ptr);
void __wrap_free(void* ptr) {
  COUNT_SITE(kFree);
  __real_free(ptr);
}

int vita_syscall_counts(uint64_t* out, const char* const** names) {
  for (int i = 0; i < kSyscallCount; ++i) {
    out[i] = __atomic_load_n(&g_syscalls[i], __ATOMIC_RELAXED);
  }
  *names = kSyscallNames;
  return kSyscallCount;
}

/* Called by rex::thread::global_critical_region::mutex() (core/mutex.cpp). */
void vita_count_global_lock(uintptr_t return_address) {
  Count(&g_syscalls[kGlobalLock]);
  CountSite(kGlobalLock, return_address);
}
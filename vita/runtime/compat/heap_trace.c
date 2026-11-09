/* Heap trace (diagnostic, off by default): which call paths hold the newlib
 * heap and which ones allocate and free big blocks over and over.
 *
 * newlib's heap is one block of _newlib_heap_size_user that never shrinks, so
 * blocks of changing sizes freed and allocated again leave holes: the heap can
 * be "full" with megabytes free in pieces. With vita_heap_trace_enable(min)
 * every allocation of at least `min` bytes made through malloc, calloc, realloc
 * or memalign records its call path (return addresses from the ARM unwind
 * tables) until it is freed. vita_heap_trace_dump() appends the paths holding
 * the most memory, the ones that allocated the most bytes and the last big
 * allocations and frees to a file.
 *
 * Where a frame has no unwind table (prebuilt vitaGL, C code built without
 * -funwind-tables) the rest of the path comes from a scan of the stack for
 * values inside the code segment; those addresses are printed odd (bit 0 set)
 * and can be wrong.
 *
 * malloc and free are wrapped in fast_tls.c (they also count calls); calloc,
 * realloc and memalign are wrapped here (-Wl,--wrap in vita/runtime/CMakeLists.txt).
 * Map the addresses with arm-vita-eabi-addr2line -f -C -e <elf> after
 * subtracting the load base the dump prints and adding 0x81000000. */

#include <malloc.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr/lw_mutex.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unwind-arm-common.h> /* the SDK has no unwind.h */

#include "heap_trace.h"

extern char __executable_start;
extern char etext;

#define kDepth 6          /* return addresses kept per call path */
#define kSkipFrames 2     /* vita_heap_trace_alloc and the wrapper */
#define kScanWords 64     /* stack words scanned after the unwinder stops */
#define kSiteCount 2048   /* call paths */
#define kLiveCount 32768  /* tracked live blocks (power of two) */
#define kLiveBits 15
#define kEventCount 256   /* recent big allocations and frees (power of two) */
#define kBigBytes (256u * 1024u)

typedef struct {
  uintptr_t pc[kDepth];
  uint32_t used;
  uint32_t allocs, frees, live_count;
  uint32_t live_bytes, peak_live_bytes, max_size;
  uint64_t total_bytes;
} HeapSite;

typedef struct {
  uintptr_t ptr;
  uint32_t size;
  uint32_t site;
} LiveBlock;

typedef struct {
  uint32_t ms;
  uint32_t size;
  uint16_t site;
  uint8_t op; /* 1 alloc, 2 free, 3 failed alloc */
} HeapEvent;

static volatile int g_on;
static uint32_t g_min_bytes;
static SceKernelLwMutexWork g_lock;
static HeapSite g_sites[kSiteCount];
static uint32_t g_site_used, g_site_dropped;
static LiveBlock g_live[kLiveCount];
static uint32_t g_live_used, g_live_dropped, g_failed;
static uint64_t g_live_bytes;
static HeapEvent g_events[kEventCount];
static uint32_t g_event_next;

int __real_sceKernelLockLwMutex(SceKernelLwMutexWork* work, int count, unsigned int* timeout);
int __real_sceKernelUnlockLwMutex(SceKernelLwMutexWork* work, int count);

static void Lock(void) { __real_sceKernelLockLwMutex(&g_lock, 1, NULL); }
static void Unlock(void) { __real_sceKernelUnlockLwMutex(&g_lock, 1); }

void vita_heap_trace_enable(uint32_t min_bytes) {
  if (g_on || min_bytes == 0) {
    return;
  }
  if (sceKernelCreateLwMutex(&g_lock, "heap_trace", 0, 0, NULL) < 0) {
    return;
  }
  g_min_bytes = min_bytes;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  g_on = 1;
}

int vita_heap_trace_on(void) { return g_on; }

typedef struct {
  uintptr_t pc[kDepth + kSkipFrames];
  int count;
  uintptr_t last_sp;
} Trace;

static _Unwind_Reason_Code TraceStep(_Unwind_Context* context, void* arg) {
  Trace* t = (Trace*)arg;
  if (t->count >= kDepth + kSkipFrames) {
    return _URC_END_OF_STACK;
  }
  t->pc[t->count++] = (uintptr_t)(_Unwind_GetGR(context, 15) & ~(_Unwind_Word)1);
  t->last_sp = (uintptr_t)_Unwind_GetGR(context, 13);
  return _URC_NO_REASON;
}

static void FillFromStack(Trace* t) {
  if (t->count < kSkipFrames || !t->last_sp) {
    return;
  }
  const uintptr_t lo = (uintptr_t)&__executable_start, hi = (uintptr_t)&etext;
  const uintptr_t* sp = (const uintptr_t*)(t->last_sp & ~(uintptr_t)3);
  for (int i = 0; i < kScanWords && t->count < kDepth + kSkipFrames; ++i) {
    const uintptr_t v = sp[i];
    /* Thumb return addresses are odd; the unwinder's own ones are even. */
    if ((v & 1) && v > lo && v < hi && v != (t->pc[t->count - 1] | 1)) {
      t->pc[t->count++] = v;
    }
  }
}

static uint32_t HashPath(const uintptr_t* pc) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < kDepth; ++i) {
    h = (h ^ (uint32_t)pc[i]) * 16777619u;
  }
  return h;
}

/* Index of the site of a call path; 0 (all other paths) when the table is full. */
static uint32_t FindSite(const uintptr_t* pc) {
  const uint32_t h = HashPath(pc);
  for (uint32_t i = 0; i < 64; ++i) {
    const uint32_t index = 1 + (h + i) % (kSiteCount - 1);
    HeapSite* s = &g_sites[index];
    if (!s->used) {
      memcpy(s->pc, pc, sizeof(s->pc));
      s->used = 1;
      ++g_site_used;
      return index;
    }
    if (memcmp(s->pc, pc, sizeof(s->pc)) == 0) {
      return index;
    }
  }
  ++g_site_dropped;
  g_sites[0].used = 1;
  return 0;
}

static uint32_t Home(uintptr_t ptr) { return ((uint32_t)ptr * 2654435761u) >> (32 - kLiveBits); }

static void LiveInsert(uintptr_t ptr, uint32_t size, uint32_t site) {
  uint32_t i = Home(ptr);
  while (g_live[i].ptr && g_live[i].ptr != ptr) {
    i = (i + 1) & (kLiveCount - 1);
  }
  if (g_live[i].ptr == ptr) {
    /* Freed without the wrappers (newlib's internal calls) and allocated again. */
    HeapSite* old = &g_sites[g_live[i].site];
    old->live_bytes -= g_live[i].size;
    old->live_count--;
    g_live_bytes -= g_live[i].size;
  } else if (g_live_used >= kLiveCount * 3 / 4) {
    ++g_live_dropped;
    return;
  } else {
    ++g_live_used;
  }
  g_live[i].ptr = ptr;
  g_live[i].size = size;
  g_live[i].site = site;
  HeapSite* s = &g_sites[site];
  s->live_bytes += size;
  s->live_count++;
  if (s->live_bytes > s->peak_live_bytes) {
    s->peak_live_bytes = s->live_bytes;
  }
  g_live_bytes += size;
}

/* Removes ptr (linear probing, backward-shift deletion); 0 when not tracked. */
static int LiveRemove(uintptr_t ptr, LiveBlock* out) {
  uint32_t i = Home(ptr);
  while (g_live[i].ptr != ptr) {
    if (!g_live[i].ptr) {
      return 0;
    }
    i = (i + 1) & (kLiveCount - 1);
  }
  *out = g_live[i];
  uint32_t j = i;
  for (;;) {
    j = (j + 1) & (kLiveCount - 1);
    if (!g_live[j].ptr) {
      break;
    }
    const uint32_t k = Home(g_live[j].ptr);
    /* Entry j stays when its home slot is cyclically in (i, j]. */
    const int stays = i <= j ? (i < k && k <= j) : (i < k || k <= j);
    if (!stays) {
      g_live[i] = g_live[j];
      i = j;
    }
  }
  g_live[i].ptr = 0;
  --g_live_used;
  return 1;
}

static void AddEvent(uint8_t op, uint32_t size, uint32_t site) {
  HeapEvent* e = &g_events[g_event_next++ & (kEventCount - 1)];
  e->ms = (uint32_t)(sceKernelGetProcessTimeWide() / 1000);
  e->size = size;
  e->site = (uint16_t)site;
  e->op = op;
}

/* Records an allocation (ptr == NULL: it failed). Not inlined: the number of
 * frames to skip depends on it. */
__attribute__((noinline)) void vita_heap_trace_alloc(void* ptr, size_t size) {
  if (!g_on || size < g_min_bytes) {
    return;
  }
  Trace t;
  memset(&t, 0, sizeof(t));
  _Unwind_Backtrace(TraceStep, &t);
  FillFromStack(&t);
  Lock();
  const uint32_t site = FindSite(t.pc + kSkipFrames);
  HeapSite* s = &g_sites[site];
  if (!ptr) {
    ++g_failed;
    AddEvent(3, (uint32_t)size, site);
    Unlock();
    return;
  }
  s->allocs++;
  s->total_bytes += size;
  if (size > s->max_size) {
    s->max_size = (uint32_t)size;
  }
  LiveInsert((uintptr_t)ptr, (uint32_t)size, site);
  if (size >= kBigBytes) {
    AddEvent(1, (uint32_t)size, site);
  }
  Unlock();
}

void vita_heap_trace_free(void* ptr) {
  if (!g_on || !ptr) {
    return;
  }
  Lock();
  LiveBlock b;
  if (LiveRemove((uintptr_t)ptr, &b)) {
    HeapSite* s = &g_sites[b.site];
    s->frees++;
    s->live_bytes -= b.size;
    s->live_count--;
    g_live_bytes -= b.size;
    if (b.size >= kBigBytes) {
      AddEvent(2, b.size, b.site);
    }
  }
  Unlock();
}

void* __real_calloc(size_t count, size_t size);
void* __wrap_calloc(size_t count, size_t size) {
  void* p = __real_calloc(count, size);
  vita_heap_trace_alloc(p, count * size);
  return p;
}

void* __real_memalign(size_t align, size_t size);
void* __wrap_memalign(size_t align, size_t size) {
  void* p = __real_memalign(align, size);
  vita_heap_trace_alloc(p, size);
  return p;
}

void* __real_realloc(void* ptr, size_t size);
void* __wrap_realloc(void* ptr, size_t size) {
  /* Forgotten before the call: once realloc frees it, another thread can get
   * the same address. A failed realloc leaves the old block untracked. */
  vita_heap_trace_free(ptr);
  void* p = __real_realloc(ptr, size);
  if (p) {
    vita_heap_trace_alloc(p, size);
  }
  return p;
}

/* ---- report ---------------------------------------------------------------- */

static HeapSite g_copy[kSiteCount];
static HeapEvent g_events_copy[kEventCount];
static uint8_t g_taken[kSiteCount];
static char g_text[48 * 1024];

static size_t Append(size_t at, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static size_t Append(size_t at, const char* fmt, ...) {
  if (at >= sizeof(g_text) - 1) {
    return at;
  }
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(g_text + at, sizeof(g_text) - at, fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return at;
  }
  at += (size_t)n;
  return at < sizeof(g_text) - 1 ? at : sizeof(g_text) - 1;
}

static size_t AppendPath(size_t at, const HeapSite* s) {
  for (int i = 0; i < kDepth && s->pc[i]; ++i) {
    at = Append(at, " %08X", (unsigned)s->pc[i]);
  }
  return Append(at, "\n");
}

/* The n best sites by key (0: live bytes, 1: bytes allocated, 2: peak live bytes). */
static size_t AppendTop(size_t at, int key, int n) {
  memset(g_taken, 0, sizeof(g_taken));
  for (int k = 0; k < n; ++k) {
    int best = -1;
    uint64_t best_value = 0;
    for (int i = 0; i < kSiteCount; ++i) {
      if (g_taken[i] || !g_copy[i].used) {
        continue;
      }
      const uint64_t v = key == 0   ? g_copy[i].live_bytes
                         : key == 1 ? g_copy[i].total_bytes
                                    : g_copy[i].peak_live_bytes;
      if (best < 0 || v > best_value) {
        best = i;
        best_value = v;
      }
    }
    if (best < 0 || best_value == 0) {
      break;
    }
    g_taken[best] = 1;
    const HeapSite* s = &g_copy[best];
    at = Append(at,
                "  site %4d live %6u KiB in %5u | peak %6u KiB | allocs %7u frees %7u | max "
                "%6u KiB | total %7u MiB |",
                best, (unsigned)(s->live_bytes / 1024), (unsigned)s->live_count,
                (unsigned)(s->peak_live_bytes / 1024), (unsigned)s->allocs, (unsigned)s->frees,
                (unsigned)(s->max_size / 1024), (unsigned)(s->total_bytes >> 20));
    at = AppendPath(at, s);
  }
  return at;
}

void vita_heap_trace_dump(const char* path, const char* title) {
  if (!g_on) {
    return;
  }
  Lock();
  memcpy(g_copy, g_sites, sizeof(g_copy));
  memcpy(g_events_copy, g_events, sizeof(g_events_copy));
  const uint32_t event_next = g_event_next;
  const uint32_t live_used = g_live_used, live_dropped = g_live_dropped;
  const uint32_t site_used = g_site_used, site_dropped = g_site_dropped, failed = g_failed;
  const uint64_t live_bytes = g_live_bytes;
  Unlock();
  const struct mallinfo heap = mallinfo();

  size_t at = 0;
  at = Append(at,
              "%s: heap used %u KiB, free %u KiB, arena %u KiB; traced blocks of >= %u bytes: "
              "%u live (%u KiB), %u not tracked (table full), %u failed allocations; %u call "
              "paths (%u not kept); load base %08X (subtract it and add 81000000 for addr2line)\n",
              title, (unsigned)(heap.uordblks / 1024), (unsigned)(heap.fordblks / 1024),
              (unsigned)(heap.arena / 1024), (unsigned)g_min_bytes, (unsigned)live_used,
              (unsigned)(live_bytes / 1024), (unsigned)live_dropped, (unsigned)failed,
              (unsigned)site_used, (unsigned)site_dropped,
              (unsigned)(uintptr_t)&__executable_start);
  at = Append(at, " by live bytes:\n");
  at = AppendTop(at, 0, 40);
  at = Append(at, " by bytes allocated since start:\n");
  at = AppendTop(at, 1, 25);
  at = Append(at, " by peak live bytes:\n");
  at = AppendTop(at, 2, 15);
  at = Append(at, " last allocations (+), frees (-) of >= %u KiB and failures (!):\n",
              kBigBytes / 1024);
  const uint32_t count = event_next < kEventCount ? event_next : kEventCount;
  for (uint32_t i = event_next - count; i != event_next; ++i) {
    const HeapEvent* e = &g_events_copy[i & (kEventCount - 1)];
    at = Append(at, "  %u.%03us %c%u KiB site %u\n", (unsigned)(e->ms / 1000),
                (unsigned)(e->ms % 1000), e->op == 1 ? '+' : e->op == 2 ? '-' : '!',
                (unsigned)(e->size / 1024), (unsigned)e->site);
  }
  at = Append(at, "\n");

  const SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) {
    sceIoWrite(fd, g_text, at);
    sceIoClose(fd);
  }
}

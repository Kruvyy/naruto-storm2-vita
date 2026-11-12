/* Live kernel objects of the program by kind (PS Vita).
 *
 * The console limits how many kernel objects (UIDs) a process holds: past the
 * limit, creating one fails with SCE_KERNEL_ERROR_UID_RL_OVERFLOW (0x8002450D).
 * Vita3K has no such limit, so a leak of kernel objects only shows on a
 * console, and late: Storm 2's battle load stopped at 27% twice (bug 112, 805
 * small memory blocks; bug 114, semaphores). On this SDK every std::mutex
 * makes a kernel semaphore in its constructor (pthread_mutex_init) and
 * deletes it in its destructor.
 * These wrappers (-Wl,--wrap in vita/runtime/CMakeLists.txt) count what this
 * executable creates and deletes, so the app's stats show the live number of
 * each kind in Vita3K too. Objects that system modules make for themselves
 * (SceGxm's render target memory, for example) are not seen. */

#include <errno.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <stdint.h>

#include "fast_tls.h"
#include "kernel_objects.h"

enum {
  kSema,
  kMutex,
  kEventFlag,
  kLwMutex,
  kLwCond,
  kCond,
  kThread,
  kFile,
  kDir,
  kMemBlock,
  kKinds
};
static const char* const kNames[kKinds] = {
    "semaphores", "mutexes", "event flags", "lw mutexes", "lw conds", "conds",
    "threads",    "files",   "dirs",        "memory blocks"};

typedef struct {
  int32_t live;
  uint32_t made;
  uint32_t failed;
  int32_t last_error;
} Kind;
static Kind g_kinds[kKinds];
static uint32_t g_pthread_made, g_pthread_destroyed;

static void Made(int kind, int result) {
  if (result >= 0) {
    __atomic_add_fetch(&g_kinds[kind].live, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&g_kinds[kind].made, 1, __ATOMIC_RELAXED);
  } else {
    __atomic_add_fetch(&g_kinds[kind].failed, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&g_kinds[kind].last_error, result, __ATOMIC_RELAXED);
  }
}

static void Deleted(int kind, int result) {
  if (result >= 0) {
    __atomic_sub_fetch(&g_kinds[kind].live, 1, __ATOMIC_RELAXED);
  }
}

int vita_kernel_object_counts(VitaKernelObjectCount* out, int max) {
  for (int i = 0; i < kKinds && i < max; ++i) {
    out[i].name = kNames[i];
    out[i].live = __atomic_load_n(&g_kinds[i].live, __ATOMIC_RELAXED);
    out[i].made = __atomic_load_n(&g_kinds[i].made, __ATOMIC_RELAXED);
    out[i].failed = __atomic_load_n(&g_kinds[i].failed, __ATOMIC_RELAXED);
    out[i].last_error = __atomic_load_n(&g_kinds[i].last_error, __ATOMIC_RELAXED);
  }
  return kKinds;
}

void vita_pthread_mutex_counts(uint32_t* made, uint32_t* destroyed) {
  *made = __atomic_load_n(&g_pthread_made, __ATOMIC_RELAXED);
  *destroyed = __atomic_load_n(&g_pthread_destroyed, __ATOMIC_RELAXED);
}

SceUID __real_sceKernelCreateSema(const char* name, SceUInt attr, int init, int max,
                                  SceKernelSemaOptParam* option);
SceUID __wrap_sceKernelCreateSema(const char* name, SceUInt attr, int init, int max,
                                  SceKernelSemaOptParam* option) {
  const SceUID r = __real_sceKernelCreateSema(name, attr, init, max, option);
  Made(kSema, r);
  return r;
}
int __real_sceKernelDeleteSema(SceUID id);
int __wrap_sceKernelDeleteSema(SceUID id) {
  const int r = __real_sceKernelDeleteSema(id);
  Deleted(kSema, r);
  return r;
}

SceUID __real_sceKernelCreateMutex(const char* name, SceUInt attr, int init,
                                   SceKernelMutexOptParam* option);
SceUID __wrap_sceKernelCreateMutex(const char* name, SceUInt attr, int init,
                                   SceKernelMutexOptParam* option) {
  const SceUID r = __real_sceKernelCreateMutex(name, attr, init, option);
  Made(kMutex, r);
  return r;
}
int __real_sceKernelDeleteMutex(SceUID id);
int __wrap_sceKernelDeleteMutex(SceUID id) {
  const int r = __real_sceKernelDeleteMutex(id);
  Deleted(kMutex, r);
  return r;
}

SceUID __real_sceKernelCreateEventFlag(const char* name, int attr, int bits,
                                       SceKernelEventFlagOptParam* opt);
SceUID __wrap_sceKernelCreateEventFlag(const char* name, int attr, int bits,
                                       SceKernelEventFlagOptParam* opt) {
  const SceUID r = __real_sceKernelCreateEventFlag(name, attr, bits, opt);
  Made(kEventFlag, r);
  return r;
}
int __real_sceKernelDeleteEventFlag(int id);
int __wrap_sceKernelDeleteEventFlag(int id) {
  const int r = __real_sceKernelDeleteEventFlag(id);
  Deleted(kEventFlag, r);
  return r;
}

int __real_sceKernelCreateLwMutex(SceKernelLwMutexWork* work, const char* name, unsigned int attr,
                                  int init, const SceKernelLwMutexOptParam* opt);
int __wrap_sceKernelCreateLwMutex(SceKernelLwMutexWork* work, const char* name, unsigned int attr,
                                  int init, const SceKernelLwMutexOptParam* opt) {
  const int r = __real_sceKernelCreateLwMutex(work, name, attr, init, opt);
  Made(kLwMutex, r);
  return r;
}
int __real_sceKernelDeleteLwMutex(SceKernelLwMutexWork* work);
int __wrap_sceKernelDeleteLwMutex(SceKernelLwMutexWork* work) {
  const int r = __real_sceKernelDeleteLwMutex(work);
  Deleted(kLwMutex, r);
  return r;
}

int __real_sceKernelCreateLwCond(SceKernelLwCondWork* work, const char* name, unsigned int attr,
                                 SceKernelLwMutexWork* mutex, const SceKernelLwCondOptParam* opt);
int __wrap_sceKernelCreateLwCond(SceKernelLwCondWork* work, const char* name, unsigned int attr,
                                 SceKernelLwMutexWork* mutex, const SceKernelLwCondOptParam* opt) {
  const int r = __real_sceKernelCreateLwCond(work, name, attr, mutex, opt);
  Made(kLwCond, r);
  return r;
}
int __real_sceKernelDeleteLwCond(SceKernelLwCondWork* work);
int __wrap_sceKernelDeleteLwCond(SceKernelLwCondWork* work) {
  const int r = __real_sceKernelDeleteLwCond(work);
  Deleted(kLwCond, r);
  return r;
}

SceUID __real_sceKernelCreateCond(const char* name, SceUInt attr, SceUID mutex,
                                  const SceKernelCondOptParam* option);
SceUID __wrap_sceKernelCreateCond(const char* name, SceUInt attr, SceUID mutex,
                                  const SceKernelCondOptParam* option) {
  const SceUID r = __real_sceKernelCreateCond(name, attr, mutex, option);
  Made(kCond, r);
  return r;
}
int __real_sceKernelDeleteCond(SceUID id);
int __wrap_sceKernelDeleteCond(SceUID id) {
  const int r = __real_sceKernelDeleteCond(id);
  Deleted(kCond, r);
  return r;
}

SceUID __real_sceKernelCreateThread(const char* name, SceKernelThreadEntry entry, int priority,
                                    SceSize stack, SceUInt attr, int affinity,
                                    const SceKernelThreadOptParam* option);
SceUID __wrap_sceKernelCreateThread(const char* name, SceKernelThreadEntry entry, int priority,
                                    SceSize stack, SceUInt attr, int affinity,
                                    const SceKernelThreadOptParam* option) {
  const SceUID r = __real_sceKernelCreateThread(name, entry, priority, stack, attr, affinity,
                                                option);
  Made(kThread, r);
  return r;
}
int __real_sceKernelDeleteThread(SceUID id);
int __wrap_sceKernelDeleteThread(SceUID id) {
  const int r = __real_sceKernelDeleteThread(id);
  Deleted(kThread, r);
  return r;
}
int __real_sceKernelExitDeleteThread(int status);
int __wrap_sceKernelExitDeleteThread(int status) {
  Deleted(kThread, 0);  /* the call does not return */
  return __real_sceKernelExitDeleteThread(status);
}

SceUID __real_sceIoOpen(const char* file, int flags, SceMode mode);
SceUID __wrap_sceIoOpen(const char* file, int flags, SceMode mode) {
  const SceUID r = __real_sceIoOpen(file, flags, mode);
  Made(kFile, r);
  return r;
}
int __real_sceIoClose(SceUID fd);
int __wrap_sceIoClose(SceUID fd) {
  const int r = __real_sceIoClose(fd);
  Deleted(kFile, r);
  return r;
}

SceUID __real_sceIoDopen(const char* dirname);
SceUID __wrap_sceIoDopen(const char* dirname) {
  const SceUID r = __real_sceIoDopen(dirname);
  Made(kDir, r);
  return r;
}
int __real_sceIoDclose(SceUID fd);
int __wrap_sceIoDclose(SceUID fd) {
  const int r = __real_sceIoDclose(fd);
  Deleted(kDir, r);
  return r;
}

SceUID __real_sceKernelAllocMemBlock(const char* name, SceKernelMemBlockType type, SceSize size,
                                     SceKernelAllocMemBlockOpt* opt);
SceUID __wrap_sceKernelAllocMemBlock(const char* name, SceKernelMemBlockType type, SceSize size,
                                     SceKernelAllocMemBlockOpt* opt) {
  const SceUID r = __real_sceKernelAllocMemBlock(name, type, size, opt);
  Made(kMemBlock, r);
  return r;
}
int __real_sceKernelFreeMemBlock(SceUID uid);
int __wrap_sceKernelFreeMemBlock(SceUID uid) {
  const int r = __real_sceKernelFreeMemBlock(uid);
  Deleted(kMemBlock, r);
  return r;
}

/* pthread-embedded's pthread_mutex_init makes the mutex's semaphore; its
 * pthread_mutex_destroy deletes it (a statically initialized mutex that was
 * never locked has none). */
int __real_pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
int __wrap_pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
  vita_count_mutex_init((uintptr_t)__builtin_return_address(0));
  const int r = __real_pthread_mutex_init(mutex, attr);
  if (r == 0) {
    __atomic_add_fetch(&g_pthread_made, 1, __ATOMIC_RELAXED);
  }
  return r;
}

/* The last destroys (mutex address, caller), to name who destroyed a mutex
 * first when it is destroyed again (lossy, statistics only). */
#define kDestroyRing 4096
typedef struct {
  uintptr_t mutex;
  uintptr_t caller;
} DestroyRecord;
static DestroyRecord g_destroys[kDestroyRing];
static uint32_t g_destroy_next;
static uint32_t g_double_destroys;
static VitaMutexDoubleDestroy g_double_records[8];

int vita_mutex_double_destroys(VitaMutexDoubleDestroy* out, int max) {
  const uint32_t n = __atomic_load_n(&g_double_destroys, __ATOMIC_RELAXED);
  for (uint32_t i = 0; i < n && i < 8 && (int)i < max; ++i) {
    out[i] = g_double_records[i];
  }
  return (int)n;
}

int __real_pthread_mutex_destroy(pthread_mutex_t* mutex);
int __wrap_pthread_mutex_destroy(pthread_mutex_t* mutex) {
  const uintptr_t caller = (uintptr_t)__builtin_return_address(0);
  const pthread_mutex_t before = mutex ? *mutex : NULL;
  if (mutex && before == NULL) {
    /* Already destroyed (pthread-embedded sets it to NULL): its destroy would
     * take NULL for the mutex and spin or fault on address 4. Keep the case
     * and who destroyed it before. */
    const uint32_t k = __atomic_fetch_add(&g_double_destroys, 1, __ATOMIC_RELAXED);
    if (k < 8) {
      VitaMutexDoubleDestroy* rec = &g_double_records[k];
      rec->mutex = (uintptr_t)mutex;
      rec->caller = caller;
      rec->first_caller = 0;
      const uint32_t end = __atomic_load_n(&g_destroy_next, __ATOMIC_RELAXED);
      for (uint32_t i = 1; i <= kDestroyRing; ++i) {
        const DestroyRecord* d = &g_destroys[(end - i) & (kDestroyRing - 1)];
        if (d->mutex == (uintptr_t)mutex) {
          rec->first_caller = d->caller;
          break;
        }
      }
    }
    return EINVAL;
  }
  const int initialized =
      before != NULL && (uintptr_t)before < (uintptr_t)PTHREAD_ERRORCHECK_MUTEX_INITIALIZER;
  const int r = __real_pthread_mutex_destroy(mutex);
  if (r == 0) {
    const uint32_t i = __atomic_fetch_add(&g_destroy_next, 1, __ATOMIC_RELAXED);
    g_destroys[i & (kDestroyRing - 1)].mutex = (uintptr_t)mutex;
    g_destroys[i & (kDestroyRing - 1)].caller = caller;
    if (initialized) {
      __atomic_add_fetch(&g_pthread_destroyed, 1, __ATOMIC_RELAXED);
    }
  }
  return r;
}

/* Live kernel objects of the program by kind (see kernel_objects.c). */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char* name;    /* "semaphores", "threads", ... */
  int32_t live;        /* made and not deleted yet */
  uint32_t made;       /* successful creations since start */
  uint32_t failed;     /* creations that returned an error */
  int32_t last_error;  /* the last such error (0 = none) */
} VitaKernelObjectCount;

/* Fills out[] (up to max entries); returns how many kinds there are. */
int vita_kernel_object_counts(VitaKernelObjectCount* out, int max);

/* pthread mutexes given their own semaphore (pthread_mutex_init, also run at a
 * statically initialized mutex's first lock) and destroyed again. */
void vita_pthread_mutex_counts(uint32_t* made, uint32_t* destroyed);

/* A pthread mutex destroyed again after its destroy (skipped: pthread-embedded
 * would spin or fault on it): where, and the caller of the earlier destroy if
 * it was among the last 4096 (0 = not found). */
typedef struct {
  uintptr_t mutex;
  uintptr_t caller;
  uintptr_t first_caller;
} VitaMutexDoubleDestroy;
/* Fills up to max (at most 8) records; returns how many cases there were. */
int vita_mutex_double_destroys(VitaMutexDoubleDestroy* out, int max);

#ifdef __cplusplus
}
#endif

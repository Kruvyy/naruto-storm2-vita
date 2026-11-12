/* Faster sceKernelGetTLSAddr for the PS Vita build (see fast_tls.c). */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uintptr_t tp_main;   /* TPIDRURO of the probing thread */
  uintptr_t tp_other;  /* TPIDRURO of a second thread */
  intptr_t delta;      /* TLS slot 0 minus TPIDRURO */
  int consistent;      /* every probed key had the same offset on both threads */
  int enabled;
  uint32_t real_ns;    /* one sceKernelGetTLSAddr call */
  uint32_t fast_ns;    /* the same lookup through TPIDRURO */
} VitaFastTlsReport;

typedef struct {
  uint64_t calls;       /* sceKernelGetTLSAddr calls since start (lossy count) */
  uint32_t mismatches;  /* checked calls where the fast path disagreed (turns it off) */
  int enabled;
} VitaFastTlsStats;

/* Measures the TLS layout and the cost of one call; turns the fast path on
 * when enable != 0 and the layout is the same on two threads. Returns 1 when
 * the fast path is on. */
int vita_fast_tls_probe(int enable, VitaFastTlsReport* report);
/* Compare every Nth fast lookup with the real call (0 = never). */
void vita_fast_tls_set_check(uint32_t every);
void vita_fast_tls_stats(VitaFastTlsStats* out);

/* Counts of other frequent system calls since start (lossy). Fills out[] (room
 * for VITA_SYSCALL_COUNTERS_MAX) and *names, returns how many there are. */
#define VITA_SYSCALL_COUNTERS_MAX 20
int vita_syscall_counts(uint64_t* out, const char* const** names);
/* The most frequent return addresses of counter `kind` (index into the names),
 * counted since start; returns how many were written (up to max). */
int vita_syscall_top_sites(int kind, uintptr_t* addrs, uint32_t* counts, int max);
/* Counts a pthread_mutex_init and its caller (kind "pthread_mutex_init"). */
void vita_count_mutex_init(uintptr_t return_address);

#ifdef __cplusplus
}
#endif

/* Heap trace for the PS Vita build (diagnostic, see heap_trace.c). */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts recording allocations of at least min_bytes (0 = stay off). */
void vita_heap_trace_enable(uint32_t min_bytes);
int vita_heap_trace_on(void);
/* Appends the report to path, headed by title. */
void vita_heap_trace_dump(const char* path, const char* title);

/* Called by the malloc and free wrappers (fast_tls.c). */
void vita_heap_trace_alloc(void* ptr, size_t size);
void vita_heap_trace_free(void* ptr);

#ifdef __cplusplus
}
#endif

/* POSIX FILE locking for the VitaSDK's newlib, which declares flockfile and
 * friends in <stdio.h> but does not ship them.
 *
 * This newlib is built without retargetable locks: <sys/lock.h> defines every
 * __lock_* operation as ((void) 0), so stdio itself takes no per-FILE lock.
 * These functions do exactly what newlib's stdio/flockfile.c would do in that
 * configuration (nothing). Callers that need ordering between threads must
 * lock themselves; the runtime's logger does (spdlog *_mt sinks use a mutex). */

#include <stdio.h>
#include <sys/lock.h>

void flockfile(FILE* fp) {
  _flockfile(fp);
}

int ftrylockfile(FILE* fp) {
  (void)fp;
  return 0;
}

void funlockfile(FILE* fp) {
  _funlockfile(fp);
}

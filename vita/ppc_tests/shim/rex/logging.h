// PS Vita test harness shim for <rex/logging.h>.
// Messages are written verbatim (format strings are not expanded) to the
// harness log. REX_FATAL logs and aborts, like the real macro.
#pragma once

#include <cstdlib>

extern "C" void vita_harness_log(const char* level, const char* msg);

#define REX_VITA_LOG(level, fmt, ...) vita_harness_log(level, fmt)

#define REXLOG_CRITICAL(fmt, ...) REX_VITA_LOG("CRIT", fmt)
#define REXLOG_ERROR(fmt, ...) REX_VITA_LOG("ERROR", fmt)
#define REXLOG_WARN(fmt, ...) REX_VITA_LOG("WARN", fmt)
#define REXLOG_INFO(fmt, ...) REX_VITA_LOG("INFO", fmt)
#define REXLOG_DEBUG(fmt, ...) ((void)0)
#define REXLOG_TRACE(fmt, ...) ((void)0)

#define REXCPU_ERROR(fmt, ...) REX_VITA_LOG("CPU-ERROR", fmt)
#define REXCPU_WARN(fmt, ...) REX_VITA_LOG("CPU-WARN", fmt)
#define REXCPU_INFO(fmt, ...) REX_VITA_LOG("CPU-INFO", fmt)
#define REXCPU_DEBUG(fmt, ...) ((void)0)
#define REXCPU_TRACE(fmt, ...) ((void)0)

#define REX_FATAL(fmt, ...)               \
  do {                                    \
    vita_harness_log("FATAL", fmt);       \
    std::abort();                         \
  } while (0)

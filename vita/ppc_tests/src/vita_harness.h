// Minimal test runner interface for recompiled PPC instruction tests.
#pragma once

#include <cstdint>

struct VitaTest {
  bool failed = false;
  int line = 0;
  const char* expr = nullptr;
};

using VitaTestFn = void (*)(VitaTest&);

struct VitaTestEntry {
  const char* name;
  VitaTestFn fn;
};

extern const VitaTestEntry kVitaTests[];
extern const unsigned kVitaTestCount;

// Software MMU used by the recompiled code (REX_GUEST_SOFT_MMU): `base` is a
// table of 65536 host pointers, one per 64 KiB guest page.
uint8_t* vita_guest_base();
uint8_t* vita_guest_ptr(uint32_t guest_address);

#define VITA_GUEST_BYTE(addr) (*vita_guest_ptr(static_cast<uint32_t>(addr)))

#define VITA_CHECK(cond_)         \
  do {                            \
    if (!(cond_)) {               \
      vt.failed = true;           \
      vt.line = __LINE__;         \
      vt.expr = #cond_;           \
      return;                     \
    }                             \
  } while (0)

// Storm 2: helpers for the native versions of hot guest functions written by
// scripts/ppc_to_native.py (storm2_audio_native.cpp, storm2_native_skin.cpp).
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include <simde/arm/neon.h>

#include <rex/ppc/context.h>
#include <rex/ppc/intrinsics.h>
#include <rex/system/xmemory.h>

namespace storm2_native {

inline uint8_t* Host(uint8_t* base, uint32_t address) {
  return rex::memory::GuestPtr<uint8_t*>(base, address);
}
inline uint32_t Load16(uint8_t* base, uint32_t address) {
  uint16_t v;
  std::memcpy(&v, Host(base, address), 2);
  return __builtin_bswap16(v);
}
inline uint32_t Load32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, Host(base, address), 4);
  return __builtin_bswap32(v);
}
inline void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(Host(base, address), &value, 4);
}
inline float LoadF(uint8_t* base, uint32_t address) {
  return std::bit_cast<float>(Load32(base, address));
}
inline void StoreF(uint8_t* base, uint32_t address, float value) {
  Store32(base, address, std::bit_cast<uint32_t>(value));
}

// Stack slots of the native function's own frame (host byte order).
inline uint32_t StackLoad32(const uint8_t* stack, uint32_t offset) {
  uint32_t v;
  std::memcpy(&v, stack + offset, 4);
  return v;
}
inline void StackStore32(uint8_t* stack, uint32_t offset, uint32_t value) {
  std::memcpy(stack + offset, &value, 4);
}
inline uint64_t StackLoad64(const uint8_t* stack, uint32_t offset) {
  uint64_t v;
  std::memcpy(&v, stack + offset, 8);
  return v;
}
inline void StackStore64(uint8_t* stack, uint32_t offset, uint64_t value) {
  std::memcpy(stack + offset, &value, 8);
}

// VMX vectors as four floats in guest element order (element i in lane i):
// one 32-bit byte swap per lane. The recompiled code keeps them in reversed
// lane order and reverses all 16 bytes with a table shuffle (two vtbl and a
// mask load on ARMv7) at every lvx/stvx.
using V4 = simde_float32x4_t;
// lvx: 16 bytes at (address & ~15); a 16-byte aligned block never crosses a
// guest page, so one translation covers it.
inline V4 LoadV(uint8_t* base, uint32_t address) {
  const uint8_t* p = Host(base, address & ~15u);
  return simde_vreinterpretq_f32_u8(simde_vrev32q_u8(simde_vld1q_u8(p)));
}
inline void StoreV(uint8_t* base, uint32_t address, V4 v) {
  simde_vst1q_u8(Host(base, address & ~15u), simde_vrev32q_u8(simde_vreinterpretq_u8_f32(v)));
}
// vspltw: element i in every lane.
template <int i>
inline V4 SplatV(V4 v) {
  return simde_vdupq_n_f32(simde_vgetq_lane_f32(v, i));
}
// vmulfp: as the recompiled code (one multiply, flush-to-zero NEON on the Vita).
inline V4 MulV(V4 a, V4 b) { return simde_vmulq_f32(a, b); }
// vmaddfp vD,vA,vC,vB = vA*vC + vB, the product rounded before the sum, as the
// recompiled code computes it (separate multiply and add).
inline V4 MaddV(V4 a, V4 c, V4 b) { return simde_vaddq_f32(simde_vmulq_f32(a, c), b); }
inline V4 AddV(V4 a, V4 b) { return simde_vaddq_f32(a, b); }
inline V4 OrV(V4 a, V4 b) {
  return simde_vreinterpretq_f32_u32(
      simde_vorrq_u32(simde_vreinterpretq_u32_f32(a), simde_vreinterpretq_u32_f32(b)));
}
// vrefp: the recompiled code's own function (exact 1/x with denormals flushed).
inline V4 RefpV(V4 a) {
  alignas(16) float v[4];
  simde_vst1q_f32(v, a);
  simde_mm_store_ps(v, rex::ppc::rex_vrefp(simde_mm_load_ps(v)));
  return simde_vld1q_f32(v);
}

inline uint32_t Load8(uint8_t* base, uint32_t address) { return *Host(base, address); }
inline void Store8(uint8_t* base, uint32_t address, uint32_t value) {
  *Host(base, address) = uint8_t(value);
}

// lvlx: the bytes from the address to the end of its 16-byte block, in the
// first elements; lvrx: the bytes of the block before the address, in the last
// elements (nothing when the address is aligned). The rest is zero.
inline V4 LoadLeftV(uint8_t* base, uint32_t address) {
  const uint32_t shift = address & 15u;
  if (shift == 0) {
    return LoadV(base, address);
  }
  const uint8_t* p = Host(base, address & ~15u);
  alignas(16) uint8_t g[16] = {};
  for (uint32_t i = 0; i < 16 - shift; ++i) {
    g[i] = p[i + shift];
  }
  return simde_vreinterpretq_f32_u8(simde_vrev32q_u8(simde_vld1q_u8(g)));
}
inline V4 LoadRightV(uint8_t* base, uint32_t address) {
  const uint32_t shift = address & 15u;
  if (shift == 0) {
    return simde_vdupq_n_f32(0.0f);
  }
  const uint8_t* p = Host(base, address & ~15u);
  alignas(16) uint8_t g[16] = {};
  for (uint32_t i = 0; i < shift; ++i) {
    g[16 - shift + i] = p[i];
  }
  return simde_vreinterpretq_f32_u8(simde_vrev32q_u8(simde_vld1q_u8(g)));
}
// stvewx: the element the address falls on, at the address rounded down to 4.
inline void StoreElementV(uint8_t* base, uint32_t address, V4 v) {
  alignas(16) uint32_t w[4];
  simde_vst1q_u32(w, simde_vreinterpretq_u32_f32(v));
  Store32(base, address & ~3u, w[(address & 15u) >> 2]);
}

// PPCContext keeps a vector register with guest element i in lane 3 - i (the
// recompiled code reverses all 16 bytes at a load).
inline V4 FromCtxV(const rex::ppc::VRegister& r) {
  const V4 x = simde_vrev64q_f32(simde_vld1q_f32(r.f32));
  return simde_vcombine_f32(simde_vget_high_f32(x), simde_vget_low_f32(x));
}
inline void ToCtxV(rex::ppc::VRegister& r, V4 v) {
  const V4 x = simde_vrev64q_f32(v);
  simde_vst1q_f32(r.f32, simde_vcombine_f32(simde_vget_high_f32(x), simde_vget_low_f32(x)));
}

// A single-precision value widened to double without the flush-to-zero mode
// the native function runs in turning a denormal into zero (lfs keeps it).
inline double WidenExact(float x) {
  const uint32_t b = std::bit_cast<uint32_t>(x);
  if ((b & 0x7F800000u) == 0 && (b & 0x007FFFFFu) != 0) [[unlikely]] {
    const double m = double(b & 0x007FFFFFu) * 0x1p-149;
    return (b >> 31) ? -m : m;
  }
  return double(x);
}

// Diagnostic store tracking (functions translated with --track-stores): the
// guest bytes a native function writes (address, size), except those in
// [exclude_lo, exclude_hi) (its stack).
struct StoreTrack {
  uint32_t exclude_lo = 0, exclude_hi = 0;
  std::vector<std::pair<uint32_t, uint32_t>> stores;
};
inline thread_local StoreTrack* tls_store_track = nullptr;
inline void TrackStore(uint32_t address, uint32_t size) {
  StoreTrack* t = tls_store_track;
  if (!t || (address >= t->exclude_lo && address < t->exclude_hi)) {
    return;
  }
  t->stores.emplace_back(address, size);
}

}  // namespace storm2_native

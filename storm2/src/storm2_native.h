// Storm 2: helpers for the native versions of hot guest functions written by
// scripts/ppc_to_native.py (storm2_audio_native.cpp, storm2_native_skin.cpp).
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

#include <simde/arm/neon.h>

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

}  // namespace storm2_native

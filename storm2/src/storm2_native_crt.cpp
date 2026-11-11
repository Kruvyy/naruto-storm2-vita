// Storm 2: native versions of the game's C library memory functions
// (storm2_native_crt).
//
// The game's memcpy (sub_8267CB60, 290 PPC instructions, called from 1303
// places) ran as recompiled code: every word a page-table lookup, a load with
// a byte swap and a store with another. On the PS Vita the menu's CPU time is
// the limit (the game's update ~115 ms a frame), much of it building draw
// commands, which copies a lot.
//
// Guest memory is backed in blocks that are contiguous on the host only within
// a block (and capped regions leave holes), so the copy goes in pieces that do
// not cross a 4 KiB guest page on either side; each piece is a plain memcpy.
// The guest function returns its destination in r3, which stays as it came.

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/xmemory.h>

#include <atomic>

namespace storm2_native_crt {
// Calls and bytes copied, read by the frame probe (storm2_frame_probe.cpp).
std::atomic<uint64_t> g_memcpy_calls{0}, g_memcpy_bytes{0};
}  // namespace storm2_native_crt

REXCVAR_DEFINE_BOOL(storm2_native_crt, true, "Storm 2",
                    "Run the game's memcpy (sub_8267CB60) as native copies instead of its "
                    "recompiled code")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REX_EXTERN(__imp__sub_8267CB60);

REX_HOOK_RAW(sub_8267CB60) {
  if (!REXCVAR_GET(storm2_native_crt)) {
    __imp__sub_8267CB60(ctx, base);
    return;
  }
  uint32_t dst = ctx.r3.u32;
  uint32_t src = ctx.r4.u32;
  uint32_t n = ctx.r5.u32;
  storm2_native_crt::g_memcpy_calls.fetch_add(1, std::memory_order_relaxed);
  storm2_native_crt::g_memcpy_bytes.fetch_add(n, std::memory_order_relaxed);
  constexpr uint32_t kPage = 4096;
  while (n) {
    const uint32_t dst_room = kPage - (dst & (kPage - 1));
    const uint32_t src_room = kPage - (src & (kPage - 1));
    const uint32_t piece = std::min(n, std::min(dst_room, src_room));
    std::memmove(rex::memory::GuestPtr<uint8_t*>(base, dst),
                 rex::memory::GuestPtr<const uint8_t*>(base, src), piece);
    dst += piece;
    src += piece;
    n -= piece;
  }
  // r3 (the destination) is unchanged: the guest function returns it.
}

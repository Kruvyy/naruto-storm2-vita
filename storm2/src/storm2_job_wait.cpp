// Storm 2: the game's "run a job and wait for it" (sub_825D6630) with a
// sleeping wait instead of its spin (storm2_job_wait_sleep_us).
//
// sub_825D6630(r3 = job counter object, r4 = job data) queues a job for the
// game's worker threads (F8000074/78/7C) and then spins on the object's
// pending count until the workers bring it to 0:
//   loc_825D66AC: lwz r11,8(r30); cmpwi cr6,r11,0; bgt cr6,loc_825D66AC
// On the PS Vita console (menus, frame probe and LR sampler, docs/bugs.md) the
// main thread spent 35-40 ms a frame there (~15% of its samples) while the
// workers shared the other two cores with the audio and GPU threads. The
// spin keeps its core busy; a sleeping wait leaves it to other threads
// (with vita_reserved_core off, the workers can then run there).
//
// The hook does what the recompiled function does, step by step (same calls,
// same stores, same 128-byte stack frame), with the final loop sleeping
// between checks. r29-r31 and lr are restored as __restgprlr_29 would.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include "storm2_probe.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/platform.h>
#include <rex/system/xmemory.h>

#if REX_PLATFORM_VITA
#include <psp2/kernel/threadmgr.h>
#endif

REXCVAR_DEFINE_INT32(storm2_job_wait_sleep_us, 0, "Storm 2",
                     "Wait for the game's jobs (sub_825D6630) by sleeping this many microseconds "
                     "between checks instead of spinning (0 = the game's spin)")
    .range(0, 10000);

namespace {

uint32_t Load32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, rex::memory::GuestPtr<const uint8_t*>(base, address), 4);
  return __builtin_bswap32(v);
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  const uint32_t v = __builtin_bswap32(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, address), &v, 4);
}

void SleepUs(int32_t us) {
#if REX_PLATFORM_VITA
  sceKernelDelayThread(SceUInt(us));
#else
  std::this_thread::sleep_for(std::chrono::microseconds(us));
#endif
}

}  // namespace

REX_EXTERN(__imp__sub_825D6630);
REX_EXTERN(sub_825D6508);
REX_EXTERN(sub_825D65A0);
REX_EXTERN(sub_827C2870);
REX_EXTERN(sub_825D0788);
REX_EXTERN(sub_825DF480);
REX_EXTERN(sub_825D2570);

REX_HOOK_RAW(sub_825D6630) {
  storm2_probe::Scope probe(storm2_probe::kJobWait);
  const int32_t sleep_us = REXCVAR_GET(storm2_job_wait_sleep_us);
  if (sleep_us <= 0) {
    __imp__sub_825D6630(ctx, base);
    return;
  }
  // mflr r12; bl __savegprlr_29
  const uint64_t saved_lr = ctx.lr;
  const auto saved_r29 = ctx.r29, saved_r30 = ctx.r30, saved_r31 = ctx.r31;
  ctx.r12.u64 = ctx.lr;
  // stwu r1,-128(r1)
  const uint32_t frame = ctx.r1.u32 - 128;
  Store32(base, frame, ctx.r1.u32);
  ctx.r1.u32 = frame;
  // mr r30,r3; mr r29,r4
  ctx.r30.u64 = ctx.r3.u64;
  ctx.r29.u64 = ctx.r4.u64;
  // bl 0x825d6508; mr. r4,r3
  ctx.lr = 0x825D6648;
  sub_825D6508(ctx, base);
  ctx.r4.u64 = ctx.r3.u64;
  if (ctx.r4.s32 != 0) {
    // lis r11,-32239; std r29,8(r4); addi r11,r11,-9288; stw r11,0(r4)
    const uint64_t r29 = ctx.r29.u64;
    Store32(base, ctx.r4.u32 + 8, uint32_t(r29 >> 32));
    Store32(base, ctx.r4.u32 + 12, uint32_t(r29));
    ctx.r11.u64 = 0xFFFFFFFF8210DBB8ull;  // -2112815104 + -9288
    Store32(base, ctx.r4.u32 + 0, ctx.r11.u32);
  }
  // mr r3,r30; bl 0x825d65a0
  ctx.r3.u64 = ctx.r30.u64;
  ctx.lr = 0x825D6668;
  sub_825D65A0(ctx, base);
  // bl 0x827c2870
  ctx.lr = 0x825D666C;
  sub_827C2870(ctx, base);
  // lis r11,-32107; mr r4,r3; addi r31,r11,26880; addi r5,r1,80; addi r3,r31,8
  ctx.r11.u64 = 0xFFFFFFFF82950000ull;
  ctx.r4.u64 = ctx.r3.u64;
  ctx.r31.u64 = 0xFFFFFFFF82956900ull;
  ctx.r5.s64 = ctx.r1.s64 + 80;
  ctx.r3.s64 = ctx.r31.s64 + 8;
  // bl 0x825d0788
  ctx.lr = 0x825D6684;
  sub_825D0788(ctx, base);
  if (ctx.r3.u32 != 0) {
    // li r11,1; stw r11,4(r31)
    ctx.r11.u64 = 1;
    Store32(base, ctx.r31.u32 + 4, 1);
  }
  // mr r4,r29; lwz r3,80(r1); bl 0x825df480
  ctx.r4.u64 = ctx.r29.u64;
  ctx.r3.u64 = Load32(base, ctx.r1.u32 + 80);
  ctx.lr = 0x825D66A0;
  sub_825DF480(ctx, base);
  // mr r4,r3; mr r3,r31; bl 0x825d2570
  ctx.r4.u64 = ctx.r3.u64;
  ctx.r3.u64 = ctx.r31.u64;
  ctx.lr = 0x825D66AC;
  sub_825D2570(ctx, base);
  // loc_825D66AC: lwz r11,8(r30); cmpwi cr6,r11,0; bgt cr6,loc_825D66AC
  // (here: sleep between the checks)
  const uint32_t pending = ctx.r30.u32 + 8;
  for (;;) {
    const uint32_t count = Load32(base, pending);
    ctx.r11.u64 = count;
    if (int32_t(count) <= 0) {
      break;
    }
    SleepUs(sleep_us);
  }
  // addi r1,r1,128; b __restgprlr_29
  ctx.r1.u32 = frame + 128;
  ctx.r29 = saved_r29;
  ctx.r30 = saved_r30;
  ctx.r31 = saved_r31;
  ctx.lr = saved_lr;
  ctx.r12.u64 = saved_lr;
}

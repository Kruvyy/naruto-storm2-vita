// Storm 2: optional skipping of the game's movies (storm2_skip_movies).
//
// The movies (data/movie/XENON/*.wmv: the CyberConnect2 and Dolby logos, the
// opening and four story movies) are decoded by the Xbox SDK WMV decoder
// linked into the game, as guest code. On the PS Vita that runs far below
// real time (~0.2-0.3 frames/s in Vita3K), so the option lets the game go on
// as if each movie had ended.
//
// sub_8255F820 starts a movie (verified in the recompiled code):
//   r3 = movie manager (0x82901A14), r4 = index into the movie table at
//   0x820E2938 (32-byte entries, the name pointer first).
// It returns 0 at once if a movie is already set (manager+4); otherwise it
// first stores its r5 argument in the global 0x8288BAA4 (sub_8264F738 ->
// sub_82620170), which the game's allocator (sub_82620180) uses to pick the
// heap of tagged allocations, then sets manager+4 to the table entry, opens
// the .wmv through the decoder at manager+32, starts the movie's sound (the
// .nub of the same name), sets manager+0 = 1 and returns 1. The callers then
// wait while manager+4 is set (sub_8255F208), so a movie that is never started
// reads as finished, without its picture or its sound. The heap selection is
// kept: without it the scene after the movies stayed white on the PC.
// Hiding the .wmv files instead does not work: the entry stays set and the
// game waits on a white screen.

#include <cstdint>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(storm2_skip_movies, false, "Storm 2",
                    "Skip the game's movies (logos, opening and story movies) as if they had "
                    "ended: they are decoded by the game's own code, far below real time on "
                    "the PS Vita");

namespace {

constexpr uint32_t kMovieTable = 0x820E2938;
constexpr uint32_t kMovieEntrySize = 32;
constexpr uint32_t kMovieCount = 7;  // data/movie/XENON holds 7 movies

uint32_t LoadBe32(uint8_t* base, uint32_t address) {
  uint32_t v = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    v = (v << 8) | *rex::memory::GuestPtr<const uint8_t*>(base, address + i);
  }
  return v;
}

std::string MovieName(uint8_t* base, uint32_t index) {
  if (index >= 64) {
    return "?";
  }
  const uint32_t name = LoadBe32(base, kMovieTable + index * kMovieEntrySize);
  if (name < 0x82000000u || name >= 0x82B30000u) {  // inside the game image
    return "?";
  }
  std::string text;
  for (uint32_t i = 0; i < 32; ++i) {
    const char c = char(*rex::memory::GuestPtr<const uint8_t*>(base, name + i));
    if (!c) {
      break;
    }
    text += c;
  }
  return text;
}

}  // namespace

REX_EXTERN(__imp__sub_8255F820);
REX_EXTERN(sub_82620170);  // stores r3 in the allocator's heap selector

REX_HOOK_RAW(sub_8255F820) {
  if (!REXCVAR_GET(storm2_skip_movies)) {
    __imp__sub_8255F820(ctx, base);
    return;
  }
  const uint32_t manager = ctx.r3.u32;
  if (LoadBe32(base, manager + 4) != 0) {
    ctx.r3.u64 = 0;  // a movie is already set: the original does nothing either
    return;
  }
  const uint32_t index = ctx.r4.u32;
  REXLOG_INFO("Storm 2: movie {} ({}) skipped (storm2_skip_movies){}", index,
              MovieName(base, index), index < kMovieCount ? "" : ", index past the known movies");
  ctx.r3.u64 = ctx.r5.u64;
  sub_82620170(ctx, base);
  ctx.r3.u64 = 1;
}

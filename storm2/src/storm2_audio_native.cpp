// Storm 2: native versions of the audio engine's hottest functions.
//
// The audio thread (XThread F80000B0) runs at ~177 M guest instructions/s on
// the PC; on the PS Vita the recompiled code would need several cores. Two
// functions take ~78% of its busy time (profile in docs/logs/H1_pc_audio):
//   sub_82599010 (~50%): a per-sample effect, one loop of ~750 single-
//     precision float instructions per sample over 512-entry delay lines in a
//     state structure of ~220 KiB (a reverb, by its shape). Arguments: r3 =
//     state, r4/r5 = input pointers, r6 = output buffer (one float per
//     sample), r7 = sample count, f1 = a level. It first calls sub_82596178
//     (state + 8) and sub_82595BA0 (state + 0x10024).
//   sub_8281E500 (~28%): not rewritten yet.
//
// The native version is produced from the recompiled code by
// scripts/ppc_to_native.py at build time (storm2/generated_native, derived
// from the game, never committed): guest registers become locals, the stack
// frame a local array, the float math single precision. See that script for
// the exact differences (fmadds rounding).
//
// storm2_native_audio_check runs both versions on every call from the same
// memory and compares what they wrote (state structure and output buffer).

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(storm2_native_audio, true, "Storm 2",
                    "Run native versions of the audio engine's hottest functions instead of "
                    "their recompiled code (same results; faster)");
REXCVAR_DEFINE_BOOL(storm2_native_audio_check, false, "Storm 2",
                    "Diagnostic: run the recompiled and the native audio functions on every call "
                    "from the same memory and log the differences in what they write");
REXCVAR_DEFINE_BOOL(storm2_native_audio_check_self, false, "Storm 2",
                    "With storm2_native_audio_check: compare the native version with itself "
                    "(differences then come from other threads writing the same memory)");
REXCVAR_DEFINE_BOOL(storm2_skip_reverb, false, "Storm 2",
                    "Do not run the audio engine's per-sample effect (sub_82599010, a reverb): "
                    "its buffer passes through unchanged. On the PS Vita console the native "
                    "version alone took 88% of a core at 500 MHz and the loading screen waited "
                    "for the audio thread; the Vita's sound output is discarded anyway");
REXCVAR_DEFINE_INT32(storm2_native_audio_stats, 0, "Storm 2",
                     "Log the time spent in the hooked audio functions every N seconds (0 = off)");

namespace storm2_native {

inline uint8_t* Host(uint8_t* base, uint32_t address) {
  return rex::memory::GuestPtr<uint8_t*>(base, address);
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
// The function's own stack frame (host byte order: nothing else reads it).
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

}  // namespace storm2_native

#include "sub_82599010.inc"

namespace {

// Guest memory regions copied page by page (a region can span host blocks).
struct Region {
  uint32_t address;
  uint32_t size;
};

void CopyFromGuest(uint8_t* base, const Region& region, uint8_t* out) {
  uint32_t done = 0;
  while (done < region.size) {
    const uint32_t address = region.address + done;
    const uint32_t chunk = std::min(region.size - done, 0x1000u - (address & 0xFFFu));
    std::memcpy(out + done, storm2_native::Host(base, address), chunk);
    done += chunk;
  }
}

void CopyToGuest(uint8_t* base, const Region& region, const uint8_t* in) {
  uint32_t done = 0;
  while (done < region.size) {
    const uint32_t address = region.address + done;
    const uint32_t chunk = std::min(region.size - done, 0x1000u - (address & 0xFFFu));
    std::memcpy(storm2_native::Host(base, address), in + done, chunk);
    done += chunk;
  }
}

// Running totals of the comparison and of the time spent.
struct Stats {
  uint64_t calls = 0, samples = 0;
  double ms = 0;
  uint64_t checked_calls = 0, differing_calls = 0, differing_words = 0, compared_words = 0;
  uint32_t dumped_calls = 0;
  uint64_t audible_words = 0;  // differing words with |value| >= 1e-6 on both sides
  double audible_max_rel = 0;
  double max_abs = 0, max_rel = 0;
  uint32_t worst_offset = 0;
  std::chrono::steady_clock::time_point last_log = std::chrono::steady_clock::now();
};

void LogStats(const char* name, Stats& s, bool native) {
  const int32_t interval = REXCVAR_GET(storm2_native_audio_stats);
  const auto now = std::chrono::steady_clock::now();
  if (interval <= 0 ||
      std::chrono::duration<double>(now - s.last_log).count() < double(interval)) {
    return;
  }
  const double seconds = std::chrono::duration<double>(now - s.last_log).count();
  REXLOG_INFO("Storm 2 audio: {} ({}) {} calls, {} samples, {:.1f} ms in {:.1f} s ({:.2f}% of one "
              "core, {:.2f} us per call)",
              name, native ? "native" : "recompiled", s.calls, s.samples, s.ms, seconds,
              100.0 * s.ms / 1000.0 / seconds, s.calls ? 1000.0 * s.ms / double(s.calls) : 0.0);
  s.calls = s.samples = 0;
  s.ms = 0;
  s.last_log = now;
}

// Runs the recompiled function and the native one from the same memory and
// compares what they wrote. The native results are kept.
void CheckAgainstOriginal(PPCContext& ctx, uint8_t* base, const std::vector<Region>& regions,
                          void (*original)(PPCContext&, uint8_t*),
                          void (*native)(PPCContext&, uint8_t*), Stats& s, const char* name) {
  size_t total = 0;
  for (const Region& r : regions) {
    total += r.size;
  }
  std::vector<uint8_t> before(total), after_original(total), after_native(total);
  size_t at = 0;
  for (const Region& r : regions) {
    CopyFromGuest(base, r, before.data() + at);
    at += r.size;
  }
  const PPCContext entry = ctx;
  original(ctx, base);
  // All results first, then the restore (regions may overlap).
  at = 0;
  for (const Region& r : regions) {
    CopyFromGuest(base, r, after_original.data() + at);
    at += r.size;
  }
  at = 0;
  for (const Region& r : regions) {
    CopyToGuest(base, r, before.data() + at);
    at += r.size;
  }
  ctx = entry;
  native(ctx, base);
  at = 0;
  for (const Region& r : regions) {
    CopyFromGuest(base, r, after_native.data() + at);
    at += r.size;
  }
  // Word by word, as big-endian floats where both are finite.
  uint64_t differing = 0;
  const bool dump = s.dumped_calls < 3;
  uint32_t dumped_words = 0;
  if (dump) {
    std::string per_region;
    size_t start = 0;
    for (const Region& r : regions) {
      uint32_t count = 0;
      for (size_t i = start; i + 4 <= start + r.size; i += 4) {
        if (std::memcmp(after_original.data() + i, after_native.data() + i, 4)) {
          ++count;
        }
      }
      per_region += fmt::format(" [{:08X}+{:X}: {}]", r.address, r.size, count);
      start += r.size;
    }
    REXLOG_INFO("Storm 2 audio check: {} call {}: r3 {:08X} r4 {:08X} r5 {:08X} r6 {:08X} r7 {} "
                "f1 {:016X} ({:g}); differing words per region:{}",
                name, s.checked_calls + 1, entry.r3.u32, entry.r4.u32, entry.r5.u32, entry.r6.u32,
                entry.r7.u32, std::bit_cast<uint64_t>(entry.f1.f64), entry.f1.f64, per_region);
  }
  for (size_t i = 0; i + 4 <= total; i += 4) {
    uint32_t a, b;
    std::memcpy(&a, after_original.data() + i, 4);
    std::memcpy(&b, after_native.data() + i, 4);
    if (a == b) {
      continue;
    }
    ++differing;
    const float fa = std::bit_cast<float>(__builtin_bswap32(a));
    const float fb = std::bit_cast<float>(__builtin_bswap32(b));
    if (dump && dumped_words < 12) {
      uint32_t before_word;
      std::memcpy(&before_word, before.data() + i, 4);
      REXLOG_INFO("Storm 2 audio check: {} call {} byte {}: before {:08X} recompiled {:08X} ({:g}) "
                  "native {:08X} ({:g})",
                  name, s.checked_calls + 1, i, __builtin_bswap32(before_word),
                  __builtin_bswap32(a), fa, __builtin_bswap32(b), fb);
      ++dumped_words;
    }
    double abs_diff = INFINITY, rel_diff = INFINITY;
    if (std::isfinite(fa) && std::isfinite(fb)) {
      abs_diff = std::fabs(double(fa) - double(fb));
      const double scale = std::max(std::fabs(double(fa)), std::fabs(double(fb)));
      rel_diff = scale > 0 ? abs_diff / scale : 0;
    }
    if (rel_diff > s.max_rel || (rel_diff == s.max_rel && abs_diff > s.max_abs)) {
      s.max_rel = rel_diff;
      s.max_abs = abs_diff;
      s.worst_offset = uint32_t(i);
    }
    // Words that hold audible-range floats on both sides (integers, counters
    // and near-zero tails excluded).
    if (std::isfinite(fa) && std::isfinite(fb) && std::fabs(fa) >= 1e-6f &&
        std::fabs(fb) >= 1e-6f) {
      ++s.audible_words;
      s.audible_max_rel = std::max(s.audible_max_rel, rel_diff);
    }
  }
  if (dump && differing) {
    ++s.dumped_calls;
  }
  ++s.checked_calls;
  s.compared_words += total / 4;
  if (differing) {
    ++s.differing_calls;
    s.differing_words += differing;
  }
  if (s.checked_calls == 1 || (s.checked_calls % 500) == 0) {
    REXLOG_INFO("Storm 2 audio check: {} {} calls compared ({} words each), {} with differences, "
                "{} differing words in all; largest difference {:.3g} (relative {:.3g}) at byte "
                "{} of the compared regions; floats >= 1e-6 on both sides: {} differing, largest "
                "relative difference {:.3g}",
                name, s.checked_calls, total / 4, s.differing_calls, s.differing_words,
                s.max_abs, s.max_rel, s.worst_offset, s.audible_words, s.audible_max_rel);
  }
}

Stats g_reverb_stats;

}  // namespace

REX_EXTERN(__imp__sub_82599010);

REX_HOOK_RAW(sub_82599010) {
  if (REXCVAR_GET(storm2_skip_reverb)) {
    return;  // the in/out buffer keeps the dry signal
  }
  const bool native = REXCVAR_GET(storm2_native_audio);
  const uint32_t samples = ctx.r7.u32;
  const bool timed = REXCVAR_GET(storm2_native_audio_stats) > 0;
  const auto start = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  if (native && REXCVAR_GET(storm2_native_audio_check)) {
    // The state object (its loop reaches past 0x38000 with large displacements;
    // r6 points into it, at +0x3D9B0 in every call seen) through the end of
    // r6's buffer, and the in/out buffer r4 (r5 has been the same buffer).
    const uint32_t state = ctx.r3.u32;
    uint32_t state_end = state + 0x40000;
    if (ctx.r6.u32 > state && ctx.r6.u32 + samples * 4 > state_end) {
      state_end = ctx.r6.u32 + samples * 4;
    }
    std::vector<Region> regions = {{state, state_end - state}};
    auto add_buffer = [&](uint32_t address) {
      for (const Region& r : regions) {
        if (address >= r.address && address + samples * 4 + 16 <= r.address + r.size) {
          return;  // already covered
        }
      }
      regions.push_back({address, samples * 4 + 16});
    };
    add_buffer(ctx.r6.u32);
    add_buffer(ctx.r4.u32);
    add_buffer(ctx.r5.u32);
    CheckAgainstOriginal(ctx, base, regions,
                         REXCVAR_GET(storm2_native_audio_check_self) ? Native_sub_82599010
                                                                     : __imp__sub_82599010,
                         Native_sub_82599010, g_reverb_stats, "sub_82599010");
  } else if (native) {
    Native_sub_82599010(ctx, base);
  } else {
    __imp__sub_82599010(ctx, base);
  }
  if (timed) {
    g_reverb_stats.ms += std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - start).count();
    ++g_reverb_stats.calls;
    g_reverb_stats.samples += samples;
    LogStats("sub_82599010", g_reverb_stats, native);
  }
}

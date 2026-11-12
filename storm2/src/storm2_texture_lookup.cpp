// Storm 2: the material texture lookup (sub_827BD598), with a fallback for the
// resource group that the game leaves unset on some render threads.
//
// sub_827BD598(r3 = texture entry of a material, r4 = group) resolves the
// entry's reference (u32 at +0) through the game's resource table
// (sub_825D58C0, table 0x82990900). References from 0x10000000 up are global
// ids; smaller ones are a texture index << 8 that gets the group added (the
// stage's materials in the free battle: 0xE300 + 1, 0xEA00 + 1). The result is
// cached in the entry (+4, flag 0x4000 at +10); 0 when the key is not there.
//
// The group comes from the per-frame material object (+28), which its
// constructor (sub_827C2B98, ~670k a battle run) takes from a per-thread
// variable (slot 16 of the thread's TLS block, read by sub_825D5388). The
// render code (sub_8266ACD8) sets that variable only for some objects, so the
// group an object gets is whatever its render thread was left with. A render
// job on a thread that has not rendered such an object yet builds the object
// with group 0; the caller (sub_827C4760) then calls a virtual function of the
// 0 result: a call to address 0 (PC: "read of guest 0x00000000"; Vita:
// InvalidFunctionTrap in a job thread, the other threads wait for it). Seen on
// the PC build at the battle's loading with halved stage textures in 6 of 7
// runs (also on 2 cores) and with the original data in none: which thread runs
// which job changes with timing.
//
// In the failing runs the texture was not in the table under any group 1-15
// either: the model was drawn while the battle loaded, before that texture
// was registered (index 0xE4 missing while 0xE3 and 0xEA were there).
//
// storm2_texture_group_retry: when a relative reference with group 0 finds
// nothing, the lookup is made again with groups 1-15 and the first hit is kept
// (the game caches it in the entry). storm2_texture_missing_dummy: when it is
// still not found, a stand-in object whose methods do nothing is returned
// instead of 0 and not cached, so that frame draws without the texture and a
// later frame finds it. Lookups that find their texture are not changed.
// storm2_texture_probe: diagnostics.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/perf/guest_frame_stats.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(storm2_texture_group_retry, true, "Storm 2",
                    "When a material texture reference with resource group 0 is not found "
                    "(sub_827BD598), look it up again with groups 1-15 (the group a render "
                    "thread is left with depends on timing)");
REXCVAR_DEFINE_BOOL(storm2_texture_missing_dummy, true, "Storm 2",
                    "When a material texture is still not found (not registered yet while the "
                    "battle loads), return an object whose methods do nothing instead of 0, "
                    "which the game would call (not cached: found later when registered)");
REXCVAR_DEFINE_BOOL(storm2_texture_probe, false, "Storm 2",
                    "Log the material texture lookups (sub_827BD598) of relative references, "
                    "the ones that find nothing and the material objects built with group 0");

namespace {

uint32_t ProbeLoad32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, rex::memory::GuestPtr<const uint8_t*>(base, address), 4);
  return __builtin_bswap32(v);
}

uint16_t ProbeLoad16(uint8_t* base, uint32_t address) {
  uint16_t v;
  std::memcpy(&v, rex::memory::GuestPtr<const uint8_t*>(base, address), 2);
  return __builtin_bswap16(v);
}

bool PlausibleAddress(uint32_t address) {
  return address >= 0x10000 && address < 0xFFFF0000;
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  const uint32_t v = __builtin_bswap32(value);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, address), &v, 4);
}

// An empty recompiled function (its whole body is blr), used for every method
// of the stand-in texture object.
constexpr uint32_t kEmptyGuestFunction = 0x827DC310;

// Guest object whose vtable points every slot at kEmptyGuestFunction
// (sub_827C4760 calls slot 52 of the texture object with sampler settings).
uint32_t MissingTextureObject(uint8_t* base) {
  static std::mutex m;
  static uint32_t object = 0;
  std::lock_guard<std::mutex> lock(m);
  if (object) {
    return object;
  }
  auto* memory = rex::system::kernel_state()->memory();
  constexpr uint32_t kSlots = 64;
  const uint32_t vtable = memory->SystemHeapAlloc(kSlots * 4);
  const uint32_t candidate = memory->SystemHeapAlloc(64);
  if (!vtable || !candidate) {
    return 0;
  }
  for (uint32_t i = 0; i < kSlots; ++i) {
    Store32(base, vtable + 4 * i, kEmptyGuestFunction);
  }
  for (uint32_t i = 0; i < 16; ++i) {
    Store32(base, candidate + 4 * i, 0);
  }
  Store32(base, candidate, vtable);
  object = candidate;
  return object;
}

}  // namespace

// The material object's constructor: logs the ones built with group 0
// (storm2_texture_probe only).
REX_EXTERN(__imp__sub_827C2B98);
REX_HOOK_RAW(sub_827C2B98) {
  if (!REXCVAR_GET(storm2_texture_probe)) {
    __imp__sub_827C2B98(ctx, base);
    return;
  }
  const uint32_t object = ctx.r3.u32;
  const uint32_t table = ctx.r6.u32;
  const uint32_t lr = uint32_t(ctx.lr);
  const uint32_t pcr = ctx.r13.u32;
  __imp__sub_827C2B98(ctx, base);
  static std::atomic<uint32_t> constructions{0}, with_zero{0};
  const uint32_t n = ++constructions;
  if (ProbeLoad32(base, object + 28) == 0) {
    const uint32_t z = ++with_zero;
    if (z <= 20 || z % 100000 == 0) {
      REXLOG_INFO("storm2_texture_probe: material object {:08X} built with group 0 (#{} of {}): "
                  "thread pcr {:08X} lr {:08X} table {:08X}",
                  object, z, n, pcr, lr, table);
    }
  }
}

REX_EXTERN(__imp__sub_827BD598);
REX_HOOK_RAW(sub_827BD598) {
  const bool retry = REXCVAR_GET(storm2_texture_group_retry);
  const bool probe = REXCVAR_GET(storm2_texture_probe);
  const bool dummy = REXCVAR_GET(storm2_texture_missing_dummy);
  if (!retry && !probe && !dummy) {
    __imp__sub_827BD598(ctx, base);
    return;
  }
  const uint32_t entry = ctx.r3.u32;
  const uint32_t group = ctx.r4.u32;
  const uint32_t ref = ProbeLoad32(base, entry);
  const uint16_t flags = ProbeLoad16(base, entry + 10);
  const uint32_t lr = uint32_t(ctx.lr);
  __imp__sub_827BD598(ctx, base);
  const bool relative = ref < 0x10000000 && !(flags & 0x4000);
  uint32_t found_group = group;
  if (ctx.r3.u32 == 0 && retry && relative && group == 0) {
    // The call restores lr and r1 from its frame; r3/r4 are set again.
    for (uint32_t g = 1; g < 16; ++g) {
      ctx.r3.u32 = entry;
      ctx.r4.u32 = g;
      __imp__sub_827BD598(ctx, base);
      if (ctx.r3.u32 != 0) {
        found_group = g;
        break;
      }
    }
    static std::atomic<uint32_t> fixed{0};
    const uint32_t f = ++fixed;
    if (f <= 20) {
      REXLOG_WARN("storm2_texture_group_retry: reference {:08X} of entry {:08X} had group 0: {}",
                  ref, entry,
                  ctx.r3.u32 ? fmt::format("found with group {} ({:08X})", found_group,
                                           ctx.r3.u32)
                             : std::string("not found with groups 1-15 either"));
    }
  }
  // Still nothing: the stand-in object (not cached in the entry, so the next
  // frames look the texture up again).
  if (ctx.r3.u32 == 0 && dummy) {
    const uint32_t stand_in = MissingTextureObject(base);
    static std::atomic<uint32_t> missing{0};
    const uint32_t n = ++missing;
    if (n <= 20 || n % 1000 == 0) {
      REXLOG_WARN("storm2_texture_missing_dummy: texture {:08X} (group {}) of entry {:08X} not "
                  "found, frame {}: stand-in object {:08X} (#{})",
                  ref, group, entry, rex::perf::CurrentGuestFrame(), stand_in, n);
    }
    ctx.r3.u32 = stand_in;
  }
  if (!probe) {
    return;
  }
  if (relative) {
    static std::mutex m;
    static std::unordered_map<uint32_t, uint32_t> seen;
    std::lock_guard<std::mutex> lock(m);
    uint32_t& n = seen[ref + group];
    if (n++ < 3) {
      REXLOG_INFO("storm2_texture_probe: relative ref {:08X} group {:08X} entry {:08X} -> {:08X} "
                  "(frame {}, try {})",
                  ref, group, entry, ctx.r3.u32, rex::perf::CurrentGuestFrame(), n);
    }
  }
  if (ctx.r3.u32 != 0) {
    return;
  }
  const uint32_t key = ref < 0x10000000 ? ref + group : ref;
  REXLOG_WARN("storm2_texture_probe: lookup failed: entry {:08X} ref {:08X} group {:08X} key "
              "{:08X} flags {:04X} lr {:08X}",
              entry, ref, group, key, flags, lr);
  auto dump = [&](const char* what, uint32_t address, int count) {
    if (!PlausibleAddress(address)) {
      return;
    }
    std::string line;
    for (int i = 0; i < count; ++i) {
      line += fmt::format(" {:08X}", ProbeLoad32(base, address + 4 * i));
    }
    REXLOG_WARN("storm2_texture_probe:   {} {:08X}:{}", what, address, line);
  };
  // The caller (sub_827C4760) keeps its material object in r29 (+16: the
  // texture table, 24-byte entries from +32, count at +10; +28: the group).
  const uint32_t object = ctx.r29.u32;
  dump("object (r29)", object, 16);
  if (PlausibleAddress(object)) {
    dump("texture table (+16)", ProbeLoad32(base, object + 16), 24);
  }
  if (auto l = ::rex::GetLogger()) {
    l->flush();
  }
}

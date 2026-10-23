// storm2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdio>
#include <mutex>
#include <sstream>
#include <string>

#include <rex/perf/guest_frame_stats.h>
#include <rex/perf/guest_insn_counter.h>
#include <rex/rex_app.h>

class Storm2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Storm2App>(new Storm2App(ctx, "storm2",
        PPCImageConfig));
  }

  // The executed-instruction counters live in this executable (generated code
  // built with count_guest_instructions); the runtime reads their total at
  // every VdSwap when --perf_frame_stats is on.
  void OnPostSetup() override {
    rex::perf::SetGuestInsnCounterSource(&rex::perf::GuestInsnTotal);
    rex::perf::SetPeriodicDumpCallback(&DumpThreadInsns);
  }
  // thread_insns.csv (working directory): executed guest instructions per
  // thread, with the guest thread name recorded by the runtime at XThread start.
  static void DumpThreadInsns(uint32_t frame) {
    std::lock_guard<std::mutex> lock(rex::perf::GuestInsnCounterMutex());
    FILE* f = std::fopen("thread_insns.csv", "w");
    if (!f)
      return;
    std::fprintf(f, "frame,native_thread,guest_insns,guest_thread_name\n");
    for (const auto* c : rex::perf::GuestInsnCounterList()) {
      std::ostringstream id;
      id << c->host_thread;  // MSVC STL prints the native thread id
      uint32_t native = 0;
      try {
        native = uint32_t(std::stoul(id.str()));
      } catch (...) {
      }
      std::fprintf(f, "%u,%s,%llu,\"%s\"\n", frame, id.str().c_str(),
                   (unsigned long long)c->n.load(std::memory_order_relaxed),
                   rex::perf::GuestThreadName(native).c_str());
    }
    std::fprintf(f, "%u,retired,%llu,\"\"\n", frame,
                 (unsigned long long)rex::perf::GuestInsnRetired());
    std::fclose(f);
  }
  void OnShutdown() override { rex::perf::DumpImportStats(); }

  // Other virtual hooks:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};

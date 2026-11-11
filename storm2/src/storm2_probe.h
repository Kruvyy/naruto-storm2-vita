// Storm 2: wall and CPU time of selected guest functions per frame
// (storm2_frame_probe = report interval in seconds; 0 = off).
//
// The guest-LR sampler cannot tell a thread blocked in a wait from one that
// runs host code under the same guest call; these probes time the calls
// themselves (wall clock and the calling thread's own run time).
#pragma once

#include <cstdint>

namespace storm2_probe {

enum Id : int {
  kFrameLoop,      // sub_82543EB8: the main loop's frame
  kFrameSync,      // sub_825D03F0: hand-off to the render thread and jobs
  kCounterWait,    // sub_825D5B00: wait until counter == value
  kJobWait,        // sub_825D6630
  kUpdate,         // sub_822344F8: the game's update
  kUpdateA,        // sub_82551BE8
  kUpdateA1,       // sub_8262F350
  kUpdateB,        // sub_82462AC8
  kUpdateC,        // sub_825514F8
  kUpdateC1,       // sub_82635D30
  kCount
};

bool On();
// Thread run time in microseconds (0 when probes are off).
uint64_t ThreadCpuUs();
uint64_t WallUs();
void Record(Id id, uint64_t wall_us, uint64_t cpu_us);

// Times a scope on any thread; the main thread's samples are kept apart.
struct Scope {
  Id id;
  bool on;
  uint64_t wall0 = 0, cpu0 = 0;
  explicit Scope(Id i) : id(i), on(On()) {
    if (on) {
      wall0 = WallUs();
      cpu0 = ThreadCpuUs();
    }
  }
  ~Scope() {
    if (on) {
      Record(id, WallUs() - wall0, ThreadCpuUs() - cpu0);
    }
  }
};

}  // namespace storm2_probe

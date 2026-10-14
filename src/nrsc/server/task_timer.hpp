#pragma once

#include <functional>

#include "nrsc/server/peer.hpp"

// PARTIAL reconstruction of 0x180061140, the server_manager per-frame task (run slot of vtable 0x180147b40, 8025 B,
// protected/VM). Only what docs/devirt.md 2.5 grades C is implemented: the t148 accumulate-and-reset rule and the
// peer-marking branch. Unknown (U): the t150/t154 second timer, the Y-vector branch (+0x88), the resets of
// +0x170/+0x174/+0x190, the conditions under which 0x18003f030 is called, and what the timers mean.
// SUPERSEDED (2026-10-15, docs/phase3_vm_networking.md 2.2): the full emulation shows the reset rule is `acc >= 1.0f / rate`
// for two independent limiters (acc1/rate1, acc2/rate2); the pair below is one point on that curve. The marking condition is
// "a message arrived and (flags & 0x11) == 1". session::FrameTask (src/nrsc/session/frame_task.hpp) implements the corrected
// logic and is what the DLL uses; this file is kept only for its existing tests.
namespace nrsc::server {

struct TaskTimers {
    float t148 = 0.0f;  // Timers+0x148
    float t14c = 0.0f;  // Timers+0x14c (written elsewhere: U)
};

inline constexpr float kTimerThreshold = 1.0f;            // C: float constant at 0x180145d70
inline constexpr float kResetGate = 2.0f / 3.0f;          // C: bisected, threshold ~0.66666

// t148 += dt; if t148 >= 1.0 and t14c >= 2/3 then t148 = 0. C (devirt 2.5, bisected).
void accumulate_timer(TaskTimers& timers, float dt);

// For every peer whose flag byte is exactly 1 (active, nothing else): flags = 0x11, field3c = 1, and the control
// send helper 0x18003f2c0 is called with it. C (E0/E1 sweep: flag values 2 and 0 do nothing). Returns the count.
int mark_pending_peers(PeerList& peers, const std::function<void(Peer&)>& send_control);

}  // namespace nrsc::server

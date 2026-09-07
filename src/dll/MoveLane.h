#pragma once

#include <windows.h>

// Locomotion: the thumbstick moves the player capsule.
//
// **Puppeteer the engine's movement, do not reimplement it.** The fleet
// playbook is explicit that this is where that rule pays off most: the engine
// already knows how to move a player correctly -- collision, gravity, ledges,
// AI awareness -- and a mod that writes positions fights all of it forever.
// Prey kept CryEngine's input layer (R-070), so a synthesised analog event
// reaches the same handlers a real stick does, and everything downstream is the
// game's own.
//
// The value layer is `preyvr::locomotion::StickAxis`, which is pure and tested:
// radial deadzone, change threshold, and a hard zero on release. This file is
// only the wiring -- what to hook, what to read back, and what to refuse.
//
// **The double-driving trap.** The playbook's central warning for this lane is
// that two paths driving one control never look like two inputs; they look like
// movement that is mysteriously too fast, or a deadzone that "doesn't work". A
// physical gamepad and this lane both feeding the same axis would sum. So the
// analog handlers are hooked and every call is attributed: ours, or hardware.
// `moveLeaked` is that number, and it is zero or it is not.
namespace preyvr::dll {

// 0 off, 1 observe (hook the handlers, attribute every call, post nothing),
// 2 apply (the left stick drives movement).
//
// Observe is worth running on its own: it proves the handlers fire, names the
// input object, and measures how much the player's own hardware is sending
// before we add anything to it.
DWORD SetMoveLaneMode(unsigned int mode);
unsigned int MoveLaneMode();

// Called once per rendered frame, beside the input drain.
void UpdateMoveLane();

// Radial deadzone in hundredths (default 15). The playbook's reason for radial
// rather than per-axis: a per-axis deadzone leaves diagonals live while the
// cardinals are dead, and a per-axis clamp makes diagonal movement sqrt(2)
// times faster, which reads as sprinting only when moving cornerwise.
DWORD SetMoveLaneDeadzone(unsigned int hundredths);

unsigned int MoveLaneHooked();
// Analog handler calls, split by who caused them. `native` counting up while
// this lane is applying is the double-driving symptom, named rather than felt.
unsigned long long MoveLaneOursX();
unsigned long long MoveLaneOursY();
unsigned long long MoveLaneNative();
// Events this lane handed to the input queue, and those the queue refused.
unsigned long long MoveLanePosted();
unsigned long long MoveLaneDropped();
// The input object the handlers receive, and the movement axes read back from
// it (`+0x5C`, `+0x60`, R-089) in thousandths. Read back rather than assumed:
// a value we posted that never lands here did not move the player.
unsigned long long MoveLaneInputObject();
int MoveLaneAxisMilli(unsigned int axis);   // 0 x, 1 y
// The cinematic gate the handlers test at `+0x94`. Non-zero means the engine
// is discarding movement input, so a still player is the game's decision and
// not this lane failing.
int MoveLaneCinematicGate();

} // namespace preyvr::dll

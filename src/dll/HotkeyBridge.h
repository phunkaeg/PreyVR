#pragma once

#include <windows.h>

// In-headset A/B: cycle the stereo settings from the keyboard, so comparing two
// values does not require taking the headset off.
//
// **Why this exists.** Every setting here was previously changed by tabbing out,
// describing the change, and asking what it looked like. That round trip is
// slower than the thing being measured, and it destroys the comparison it is
// meant to serve -- by the time the next value is applied the last one is a
// memory rather than a side-by-side. An A/B judgement needs the gap between the
// two to be seconds, not minutes. This was the wearer's suggestion after a
// session where two settings in a row could not be told apart.
//
// **Polled, not hooked.** A background thread reads GetAsyncKeyState. Nothing is
// hooked into Prey's input path, because a broken input hook would take the game
// with it, and this is a convenience rather than something worth that risk.
//
// **The bindings all require Ctrl+Alt**, which Prey does not use, so a stray
// press during play cannot change a setting by accident. They deliberately avoid
// the arrow keys: Ctrl+Alt+Arrow is Intel's display-rotation shortcut, and
// rotating the desktop under someone wearing a headset is a bad way to learn
// that their driver has it enabled.
//
//   Ctrl+Alt+PageUp / PageDown   eye offset up / down through the table
//   Ctrl+Alt+Home   / End        antialiasing mode up / down (0-3)
//   Ctrl+Alt+E                 swap eyes
//   Ctrl+Alt+Backspace         panic: disarm stereo submission and the camera
//
// **Feedback is the known limitation.** The wearer cannot see the current value
// from inside the headset, so the workflow is to cycle until one looks right and
// then say so -- the value is read back afterwards through the getters below.
// The press count exists to answer "is it even seeing my keys?", which is
// otherwise indistinguishable from "the setting does nothing".
namespace preyvr::dll {

DWORD SetHotkeysEnabled(unsigned int enabled);

// Eye offset in tenths of a millimetre -- 640 is 0.064 m. An integer because
// these cross the export boundary as DWORD, and a float there would be a lie
// about precision anyway.
DWORD HotkeyIpdTenthsMm();

DWORD HotkeyAaMode();

DWORD HotkeyEyeSwap();

// Accepted presses since arming. Zero while the wearer insists they are pressing
// keys means the poll is not seeing them, which is a different problem from a
// setting that does nothing.
DWORD HotkeyPressCount();

} // namespace preyvr::dll

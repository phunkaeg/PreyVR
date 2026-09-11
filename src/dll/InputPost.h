#pragma once

#include <windows.h>

// Posting synthesised input into Prey's own input layer.
//
// **This is a product feature, not a test convenience.** The end state is "no
// mouse", which means the controller has to drive menus as well as gameplay; a
// mod whose menus still need a keyboard is unfinished. Driving them through the
// engine's own pipeline is what a controller binding *is*, and R-070 established
// that Prey did not replace CryEngine's input layer, so a synthesised event
// reaches every consumer a real button does.
//
// **That last property cuts both ways.** An unscoped post reaches everything
// listening -- the playbook's shot-redirection warning applies to input too. A
// menu tap is safe because a menu is modal and its effect is immediately
// visible; gameplay actions need the player scoping that this does not yet do.
//
// **Producers enqueue; the engine thread calls.** `PostInputEvent` synchronously
// walks the listener chain and the action map -- it is not an asynchronous queue
// the engine owns. Calling it from the file-polling worker meant a thread the
// engine knows nothing about reentering that walk, which no SEH guard or counter
// addresses. `DrainQueuedInput` is called from the `CSystem::Render` hook, which
// is the main thread and, unlike the gameplay camera callback, **also runs in
// menus**. Thread ownership is asserted by construction and confirmed live: the
// drain logs its thread id once.
//
// **Reaching or returning from PostInputEvent is not acceptance evidence.** Its
// native return type is void, and the action manager hashes *input names* and
// looks up configured binds -- and the default XML pairing was never extracted
// from the PAKs, so which binds are live is a candidate, not a fact (R-089).
// Everything here reports that it posted, never that anything happened. The
// verdict comes from looking at the frame afterwards.
namespace preyvr::dll {

// Arms posting. Refuses unless the input path resolved *and* the vtable
// alignment was measured rather than assumed, because the alternative is calling
// an arbitrary virtual on a live engine object.
DWORD SetInputPostEnabled(unsigned int enabled);

// A menu tap: press then release. `action` indexes preyvr::input::MenuAction in
// declaration order (0 = Up, 1 = Down, 2 = Left, 3 = Right, 4 = Accept,
// 5 = Cancel, 6 = Start).
//
// 0 when both events were **queued** -- not when anything was delivered, and
// certainly not when a menu moved. Non-zero when refused.
// Diagnostic only. `PostInputEvent` tests posting-enabled before doing anything;
// `force` skips that test. It distinguishes "the engine never saw the event"
// from "the engine saw it and chose not to act", which is not observable from
// our own counters. The native producers do not force, so this is not the
// shipping default.
DWORD SetInputPostForce(unsigned int force);

// Nonzero epoch scopes a controller tap to the menu that produced it.
DWORD PostMenuAction(unsigned int action, unsigned long long menuEpoch = 0);

// One raw event, for finding out which keys a given screen actually listens to.
// `valueMilli` is thousandths, so a text channel can carry a float.
DWORD PostRawInput(int keyId, unsigned int state, int valueMilli);

// The same event, posted straight through instead of queued. **Drain thread
// only** -- it refuses on any other thread rather than posting from anywhere.
//
// For an analog axis the queue is the wrong shape: it deliberately drains one
// event per frame so a menu press and its release cannot collapse, while a
// two-axis stick produces two per frame. Queued, the axes outrun the drain and
// the ring fills. Collapsing is correct for an axis and wrong for a button,
// which is why these are separate calls rather than a policy flag.
DWORD PostRawInputImmediate(int keyId, unsigned int state, int valueMilli);

// Drains queued events on the calling thread. Called from the engine's own
// per-frame seam; safe to call when nothing is queued.
//
// **At most one event per frame by default.** A menu that samples its input once
// per frame would see a press and its release collapse into a single update and
// could treat the button as never held. Separate frames is the shape real input
// has.
void DrainQueuedInput();

// How many events were handed to the engine, refused before that, dropped
// because the queue was full, and the thread the drain actually ran on.
unsigned long long InputPostCount();
unsigned long long InputPostRefusedCount();
unsigned long long InputMenuDiscardedCount();
unsigned long long InputQueueDroppedCount();
unsigned long long InputQueueDepthEstimate();
// True while this thread is inside a PostInputEvent this mod issued, so a
// consumer hook can tell our synthesised input from the player's real hardware.
bool InputPostDrivingThisThread();

unsigned long InputDrainThreadId();

} // namespace preyvr::dll

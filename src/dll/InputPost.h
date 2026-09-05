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
// 0 when both events were handed to the engine. Non-zero when refused, which is
// the only failure this can honestly report.
DWORD PostMenuAction(unsigned int action);

// One raw event, for finding out which keys a given screen actually listens to.
// `valueMilli` is thousandths, so a text channel can carry a float.
DWORD PostRawInput(int keyId, unsigned int state, int valueMilli);

// How many events have been handed over, and how many were refused before that.
unsigned long long InputPostCount();
unsigned long long InputPostRefusedCount();

} // namespace preyvr::dll

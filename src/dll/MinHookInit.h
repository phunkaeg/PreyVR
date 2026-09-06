#pragma once

// One place that initialises MinHook, because relying on someone else having
// done it has now cost three live runs.
//
// **The rake.** `MH_Initialize` was called only inside the frame observer's
// enable path, so every other hook installer silently depended on the observer
// having been enabled first. When it had not, `MH_CreateHook` returned
// `MH_ERROR_NOT_INITIALIZED` and the feature reported "unavailable" for a reason
// that had nothing to do with it. This is rake #1 in the H-005 handover; it was
// documented and then walked into twice more, most recently by the input drain,
// which queued events for ever because its seam could not install.
//
// Idempotent: `MH_ERROR_ALREADY_INITIALIZED` is success, which is what makes it
// safe for every installer to call unconditionally rather than deciding whether
// it is the first.
namespace preyvr::dll {

// True when MinHook is usable. Logs once on first success and on failure.
bool EnsureMinHook();

} // namespace preyvr::dll

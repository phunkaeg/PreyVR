#pragma once

#include "preyvr/RuntimeSnapshot.h"

#include <cstdint>

namespace preyvr::dll {

// Captures the read-only engine snapshot from the live process using a
// page-validated reader. Safe to call once the landmark gate has passed; it
// never writes to game memory and fails closed on any unreadable address.
snapshot::Snapshot CaptureRuntimeSnapshot(std::uintptr_t moduleBase);

// Capture B's memory-only half: walks the pooled CRenderView array (R-033) and
// runs that entry's own stated acceptance test. Uses the same page-validated
// reader and makes no COM call on the game's device or swapchain.
//
// Reached only through the PreyVR_CaptureRenderViews export -- never on load, so
// a failure here can never be confused with a lifecycle fault during Hurdle 1.
snapshot::RenderViewCapture CaptureRenderViewsNow(std::uintptr_t moduleBase);

} // namespace preyvr::dll

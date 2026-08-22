#pragma once

#include "preyvr/RuntimeSnapshot.h"

#include <cstdint>

namespace preyvr::dll {

// Captures the read-only engine snapshot from the live process using a
// page-validated reader. Safe to call once the landmark gate has passed; it
// never writes to game memory and fails closed on any unreadable address.
snapshot::Snapshot CaptureRuntimeSnapshot(std::uintptr_t moduleBase);

} // namespace preyvr::dll

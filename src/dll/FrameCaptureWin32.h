#pragma once

#include <Windows.h>

#include <cstdint>

// Backbuffer readback, so a rendered result becomes a file we can assert on.
//
// Servicing happens inside the existing RT_EndFrame observer, which is the only
// correct place: that callback runs on the engine's render thread, and
// ID3D11Multithread protection is OFF on this device (captured 2026-08-29), so
// touching the immediate context from any other thread would be a data race
// against the engine itself.
//
// Capture is **request-based and default-off**. When no request is pending the
// per-frame cost is a single relaxed atomic load. That matters because the
// readback itself is not cheap: Map with D3D11_MAP_READ stalls the GPU, so a
// capture perturbs frame timing and must never be left armed during a
// measurement that cares about rates.
namespace preyvr::dll {

enum class FrameCaptureResult : DWORD {
    ok = 0,
    refused = 1,       // no request could be armed (already pending, or disabled)
    unavailable = 2,   // swapchain/device/context could not be resolved
    failed = 3,        // a D3D call or the file write failed
};

// Arms a one-shot capture of the next observed frame. `tag` is written into the
// dump header so an A/B pair can be told apart without relying on filenames.
// Returns refused if a request is already pending -- captures are deliberately
// not queued, because a queue would silently spread one experiment across
// frames that are not adjacent.
DWORD RequestFrameCapture(std::uint32_t tag);

// Called from the frame observer. Returns immediately when nothing is armed.
void ServiceFrameCapture(void* renderer, unsigned long long frameIndex);

// Status of the most recent capture attempt, and how many have completed.
DWORD LastFrameCaptureResult();
unsigned long long CompletedFrameCaptureCount();

} // namespace preyvr::dll

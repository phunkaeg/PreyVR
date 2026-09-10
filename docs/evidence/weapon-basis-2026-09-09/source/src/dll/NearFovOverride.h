#pragma once
#include <windows.h>

namespace preyvr::dll {
// Override the renderer's near FOV after its RT_BeginFrame latch. 1233 means
// 123.3 degrees. Accept 11..1789; zero disables. The engine restores its cvar
// value at the next frame begin. No game-thread renderer writes are performed.
// Nonzero requests install a code-gated hook; failure leaves the setting alone.
DWORD SetNearFovDeciDegrees(unsigned int deciDegrees);
DWORD NearFovDeciDegrees();
unsigned long long NearFovAppliedCount();
unsigned long long NearFovRefusedCount();
// Last native latch before our write, and validity. Equal readback does not
// prove absence of another writer; the startup cvar may equal our setting too.
// These are observations, not writer attribution. Counters are cumulative.
int NearFovObservedDeciDegrees();
bool NearFovObservedValid();
bool NearFovFaulted(); // refuses further writes until an explicit command
DWORD NearFovThreadId();
} // namespace preyvr::dll

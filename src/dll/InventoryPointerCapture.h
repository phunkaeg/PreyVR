#pragma once
#include <windows.h>
namespace preyvr::dll {
DWORD EnsureInventoryPointerCapture();
void BeginInventoryPointerEvent(int event);
void EndInventoryPointerEvent(int event);
// Cancels only a drag captured from this pointer's native mouse dispatch.
// A visible native receiver, matching item and main thread are required.
DWORD CancelInventoryPointerCapture(bool* cancelled);
}

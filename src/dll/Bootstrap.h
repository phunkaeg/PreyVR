#pragma once

#include <Windows.h>

namespace preyvr::dll {

DWORD WINAPI BootstrapMain(void* moduleParameter);
DWORD SmokeStatus();
DWORD OpenXRPreflightStatus();
DWORD ModulePinStatus();

} // namespace preyvr::dll

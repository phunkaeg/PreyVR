#pragma once

#include <Windows.h>

#include <cstdint>
#include <span>

namespace preyvr::dll {

enum class FrameObserverRuntimeStatus : DWORD {
    unavailable = 0,
    ready = 1,
    enabled = 2,
    failed = 3,
};

bool ConfigureFrameObserver(
    HMODULE preyDll,
    std::span<const std::uint8_t> mappedImage);
DWORD SetFrameObserverEnabled(bool enabled);
DWORD FrameObserverStatus();
ULONGLONG ObservedFrameCount();

} // namespace preyvr::dll

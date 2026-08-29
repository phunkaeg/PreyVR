#include "RuntimeSnapshotWin32.h"

#include "preyvr/RuntimeSnapshot.h"

#include <windows.h>

#include <cstring>

namespace preyvr::dll {
namespace {

// Reads only from committed, readable, non-guard pages. VirtualQuery is used in
// preference to SEH so the whole module stays compatible with /EHsc and so a
// bad address is a returned false rather than a swallowed fault.
//
// This is a research read of another module's memory, so it is guarded twice:
// the page state is checked here, and every pointer the capture follows has
// already been range- and alignment-checked by the pure layer.
bool PageIsReadable(std::uintptr_t address, std::size_t size)
{
    std::uintptr_t cursor = address;
    const std::uintptr_t end = address + size;
    if (end < address) {
        return false; // arithmetic overflow
    }

    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) != sizeof(info)) {
            return false;
        }
        if (info.State != MEM_COMMIT) {
            return false;
        }
        constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((info.Protect & kReadable) == 0) {
            return false;
        }
        if ((info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        const auto regionEnd =
            reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (regionEnd <= cursor) {
            return false; // no forward progress
        }
        cursor = regionEnd;
    }
    return true;
}

bool GuardedRead(std::uintptr_t address, std::span<std::uint8_t> out, void*)
{
    if (address == 0 || out.empty()) {
        return false;
    }
    if (!PageIsReadable(address, out.size())) {
        return false;
    }
    std::memcpy(out.data(), reinterpret_cast<const void*>(address), out.size());
    return true;
}

} // namespace

snapshot::Snapshot CaptureRuntimeSnapshot(std::uintptr_t moduleBase)
{
    return snapshot::Capture(moduleBase, &GuardedRead, nullptr);
}

snapshot::RenderViewCapture CaptureRenderViewsNow(std::uintptr_t moduleBase)
{
    return snapshot::CaptureRenderViews(moduleBase, &GuardedRead, nullptr);
}

} // namespace preyvr::dll

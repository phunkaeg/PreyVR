#include "MinHookInit.h"

#include "Logger.h"

#include <MinHook.h>

#include <atomic>
#include <string>

namespace preyvr::dll {
namespace {

std::atomic<bool> gReady{false};
std::atomic<bool> gLogged{false};

} // namespace

bool EnsureMinHook()
{
    if (gReady.load(std::memory_order_acquire)) {
        return true;
    }
    const MH_STATUS status = MH_Initialize();
    // Already initialised is the normal case for the second caller onwards, and
    // treating it as an error is exactly how a shared helper would reintroduce
    // the problem it exists to remove.
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        lifecycle::Log(std::string("preyvr_minhook result=failed detail=initialize status=") +
                       MH_StatusToString(status));
        return false;
    }
    gReady.store(true, std::memory_order_release);
    bool expected = false;
    if (gLogged.compare_exchange_strong(expected, true)) {
        lifecycle::Log("preyvr_minhook result=0 detail=initialised");
    }
    return true;
}

} // namespace preyvr::dll

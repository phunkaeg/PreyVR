// Use the real queue/dispatcher. A missing Prey module is the positive control:
// resolution must be attempted only by the drain, never by the producer thread.
#include "../../../src/dll/HudBridge.cpp"
#include <iostream>
#include <thread>

DWORD loggedThread = 0;
std::string lastLog;
namespace preyvr::lifecycle {
void Log(std::string_view line) { loggedThread = GetCurrentThreadId(); lastLog = line; }
}
namespace preyvr::dll { DWORD EnsureRenderHookInstalled() { return 0; } }
int main() {
    using namespace preyvr::dll;
    if (GetModuleHandleW(L"PreyDll.dll")) { return 2; }
    DWORD queued = 99;
    std::thread producer([&] {
        std::string name = "reticlePosition";
        queued = CallHudFunction(name.c_str(), 0.25f, 0.75f, true);
        name.assign("changed after enqueue");
    });
    producer.join();
    if (queued != 0 || HudRefusedCount() != 0 || HudCallCount() != 0) { return 1; }
    DrainQueuedHudCalls();
    if (HudRefusedCount() != 1 || loggedThread != GetCurrentThreadId() ||
        lastLog.find("fn=reticlePosition") == std::string::npos) { return 1; }
    for (int i = 0; i < 8; ++i) {
        if (CallHudFunction("reticleXOffset", 0.5f, 0, false) != 0) { return 1; }
    }
    if (CallHudFunction("reticleXOffset", 0.5f, 0, false) != 10) { return 1; }
    std::cout << "PASS HUD calls are owned, bounded, and resolved only on the drain thread\n";
    return 0;
}

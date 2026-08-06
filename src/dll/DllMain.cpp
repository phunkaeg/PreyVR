#include "Bootstrap.h"

#include <Windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    DisableThreadLibraryCalls(instance);
    const HANDLE thread = CreateThread(
        nullptr,
        0,
        preyvr::dll::BootstrapMain,
        instance,
        0,
        nullptr);
    if (thread == nullptr) {
        return FALSE;
    }
    CloseHandle(thread);
    return TRUE;
}

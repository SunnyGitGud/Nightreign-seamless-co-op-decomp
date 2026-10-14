// DllMain of the reconstructed nrsc.dll. Like the original it does nothing but disable thread notifications;
// all work happens in the init thread started from modengine_ext_init (init.cpp).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}

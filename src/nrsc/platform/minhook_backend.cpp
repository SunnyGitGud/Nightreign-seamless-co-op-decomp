#include "nrsc/platform/minhook_backend.hpp"

#include <stdexcept>
#include <string>

#include <MinHook.h>

namespace nrsc::platform {

MinHookBackend::MinHookBackend() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED)
        throw std::runtime_error(std::string("MH_Initialize: ") + MH_StatusToString(s));
}

void* MinHookBackend::create(void* target, void* detour) {
    void* trampoline = nullptr;
    MH_STATUS s = MH_CreateHook(target, detour, &trampoline);
    if (s != MH_OK) throw std::runtime_error(std::string("MH_CreateHook: ") + MH_StatusToString(s));
    return trampoline;
}

void MinHookBackend::enable_all() {
    MH_STATUS s = MH_EnableHook(MH_ALL_HOOKS);
    if (s != MH_OK) throw std::runtime_error(std::string("MH_EnableHook: ") + MH_StatusToString(s));
}

}  // namespace nrsc::platform

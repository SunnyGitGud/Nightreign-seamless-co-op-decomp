#pragma once
// InlineBackend over the vendored MinHook (Windows only). Original: statically linked MinHook, FUN_1800068d0 = MH_CreateHook,
// FUN_1800095f0 = MH_EnableHook (docs/hooks.md section 1, S).
#include "nrsc/hooks/hook.hpp"

namespace nrsc::platform {

class MinHookBackend final : public hooks::InlineBackend {
public:
    MinHookBackend();  // MH_Initialize once per process
    void* create(void* target, void* detour) override;
    void enable_all() override;  // MH_EnableHook(MH_ALL_HOOKS)
};

}  // namespace nrsc::platform

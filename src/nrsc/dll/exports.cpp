// The two exports of nrsc.dll (original export directory: me3_load_early @ .rdata 0x180145f00, modengine_ext_init @ 0x180001220).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

#include "nrsc/dll/init.hpp"

// ME3 reads this to load the extension early (C: u64 value 1).
extern "C" __declspec(dllexport) const std::uint64_t me3_load_early = 1;

namespace {
// ModEngine2 extension object handed back through the 2nd argument. The original stores a pointer to a static
// vtable-bearing object (.data 0x18017c310); we expose no-op virtuals (destructor, on_attach-like slots).
struct Extension {
    virtual ~Extension() = default;
    virtual void on_attach() {}
    virtual void on_detach() {}
};
Extension g_extension;
}  // namespace

// bool modengine_ext_init(void* connector, Extension** out)  (0x180001220: *rdx = &extension; starts the init thread
// with _beginthreadex(thread_proc 0x180001360), waits on the ready flag, returns true).
extern "C" __declspec(dllexport) bool modengine_ext_init(void* /*connector*/, void** extension) {
    if (extension) *extension = &g_extension;
    nrsc::dll::start_and_wait();
    return true;
}

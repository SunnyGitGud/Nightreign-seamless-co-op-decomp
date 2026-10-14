#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "nrsc/signatures/module_image.hpp"

namespace nrsc::dll {

// A snapshot of a loaded module (section table + SizeOfImage bytes) for the signature scanner. Copied region by region:
// pages that are not committed, PAGE_NOACCESS or PAGE_GUARD read as zero instead of faulting.
signatures::ModuleImage live_image(HMODULE module);

}  // namespace nrsc::dll

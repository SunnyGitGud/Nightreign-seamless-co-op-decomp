# MinHook v1.3.4 (third_party/minhook, BSD-2, commit c3fcafd): the inline-hook back end of the Windows build.
include_guard(GLOBAL)
enable_language(C)
set(_mh "${PROJECT_SOURCE_DIR}/third_party/minhook")
add_library(nrsc_minhook STATIC
  ${_mh}/src/buffer.c ${_mh}/src/hook.c ${_mh}/src/trampoline.c ${_mh}/src/hde/hde32.c ${_mh}/src/hde/hde64.c)
target_include_directories(nrsc_minhook PUBLIC ${_mh}/include)

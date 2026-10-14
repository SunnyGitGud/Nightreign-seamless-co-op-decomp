# nrsc.dll (work package 7). Included from the top-level CMakeLists when NRSC_BUILD_DLL is ON.

if(NOT WIN32)
  message(FATAL_ERROR "NRSC_BUILD_DLL needs a Windows target: use -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-clang-cl-xwin.cmake")
endif()

if(NOT TARGET nrsc_minhook)
  enable_language(C)
  set(_mh ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook)
  add_library(nrsc_minhook STATIC
    ${_mh}/src/buffer.c ${_mh}/src/hook.c ${_mh}/src/trampoline.c ${_mh}/src/hde/hde64.c)
  target_include_directories(nrsc_minhook PUBLIC ${_mh}/include)
endif()

set(NRSC_DLL_SOURCES
  src/nrsc/dll/dllmain.cpp
  src/nrsc/dll/exports.cpp
  src/nrsc/dll/init.cpp
  src/nrsc/dll/session_driver.cpp
  src/nrsc/dll/crash_log.cpp
)

add_library(nrsc_dll SHARED ${NRSC_DLL_SOURCES})
set_target_properties(nrsc_dll PROPERTIES OUTPUT_NAME nrsc PREFIX "")
target_link_libraries(nrsc_dll PRIVATE nrsc_core nrsc_minhook kernel32 user32 psapi)
# Static release CRT (/MT) for all configs (xwin ships no debug CRT libs).
set_property(TARGET nrsc_dll nrsc_core nrsc_minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_link_options(nrsc_dll PRIVATE /MANIFEST:NO /MAP:${CMAKE_BINARY_DIR}/nrsc.map)

# nrsc.dll (work package 7). Included from the top-level CMakeLists when NRSC_BUILD_DLL is ON.
if(NOT WIN32)
  message(FATAL_ERROR "NRSC_BUILD_DLL needs a Windows target: use -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-clang-cl-xwin.cmake")
endif()
include(${CMAKE_CURRENT_LIST_DIR}/minhook.cmake)

add_library(nrsc_dll SHARED
  src/nrsc/dll/dllmain.cpp
  src/nrsc/dll/exports.cpp
  src/nrsc/dll/init.cpp
  src/nrsc/dll/session_driver.cpp
  src/nrsc/dll/crash_log.cpp
  src/nrsc/dll/fatal_box.cpp
  src/nrsc/dll/live_image.cpp
  src/nrsc/dll/log.cpp
)
set_target_properties(nrsc_dll PROPERTIES OUTPUT_NAME nrsc PREFIX "")
target_link_libraries(nrsc_dll PRIVATE nrsc_core nrsc_minhook kernel32 user32 psapi)
# Static release CRT (/MT) for all configs (xwin ships no debug CRT libs).
set_property(TARGET nrsc_dll nrsc_core nrsc_minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_link_options(nrsc_dll PRIVATE /MANIFEST:NO /MAP:${CMAKE_BINARY_DIR}/nrsc.map)

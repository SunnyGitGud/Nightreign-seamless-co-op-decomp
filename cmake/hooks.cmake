# hooks module (WP6): installer + platform code memory (+ MinHook back end on Windows).
nrsc_add_module(SOURCES src/nrsc/hooks/hook_filter.cpp TESTS tests/hooks/hook_filter_test.cpp)
if(WIN32)
  include(${CMAKE_CURRENT_LIST_DIR}/minhook.cmake)
  nrsc_add_module(SOURCES src/nrsc/hooks/installer.cpp src/nrsc/platform/win_code_memory.cpp
                          src/nrsc/platform/minhook_backend.cpp)
  target_link_libraries(nrsc_core PUBLIC nrsc_minhook)
else()
  # The installer test patches real RWX pages (mmap), so it only runs on POSIX.
  nrsc_add_module(SOURCES src/nrsc/hooks/installer.cpp src/nrsc/platform/posix_code_memory.cpp
                  TESTS tests/hooks/installer_test.cpp)
endif()

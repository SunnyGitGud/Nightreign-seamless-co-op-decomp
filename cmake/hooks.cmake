# hooks module (WP6): installer + platform code memory (+ MinHook back end on Windows).
# include()d from the top-level CMakeLists after nrsc_core and nrsc_tests exist, so paths are source-dir relative.
target_sources(nrsc_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/hooks/installer.cpp
)
if(WIN32)
  target_sources(nrsc_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/platform/win_code_memory.cpp)
  # MinHook v1.3.4 (third_party/minhook), Windows only. The DLL target (WP7) links nrsc_core, which links nrsc_minhook publicly.
  enable_language(C)
  add_library(nrsc_minhook STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/src/buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/src/hook.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/src/trampoline.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/src/hde/hde32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/src/hde/hde64.c)
  target_include_directories(nrsc_minhook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/include)
  target_sources(nrsc_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/platform/minhook_backend.cpp)
  target_link_libraries(nrsc_core PUBLIC nrsc_minhook)
else()
  target_sources(nrsc_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/platform/posix_code_memory.cpp)
  target_sources(nrsc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/hooks/installer_test.cpp)
endif()

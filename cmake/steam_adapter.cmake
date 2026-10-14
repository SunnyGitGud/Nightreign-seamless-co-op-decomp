# steam adapter (src/nrsc/platform/steam). include()d from the top-level CMakeLists (NRSC_MODULES) after nrsc_core and
# nrsc_tests exist. The decode/dispatch helpers are portable (Linux tests); the flat-API adapter is WIN32 only.
target_sources(nrsc_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/platform/steam/steam_decode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/platform/steam/steam_callbacks.cpp
)
if(WIN32)
  # Compiled into nrsc_core (static lib, so the compile check runs in every Windows build; linked into nrsc.dll only when
  # referenced). Uses GetModuleHandleW/GetProcAddress from kernel32, no steam_api import library.
  target_sources(nrsc_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/platform/steam/steam_adapter_win.cpp)
endif()
if(TARGET nrsc_tests)
  target_sources(nrsc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/steam_decode_test.cpp)
endif()

# steam adapter (src/nrsc/platform/steam). The decode/dispatch helpers are portable (Linux tests); the flat-API adapter is
# WIN32 only. It is compiled into nrsc_core (a static lib, so the compile check runs in every Windows build; it is linked
# into nrsc.dll only when referenced) and uses GetModuleHandleW/GetProcAddress, no steam_api import library.
nrsc_add_module(
  SOURCES src/nrsc/platform/steam/steam_decode.cpp src/nrsc/platform/steam/steam_callbacks.cpp
  WIN32_SOURCES src/nrsc/platform/steam/steam_adapter_win.cpp
  TESTS tests/platform/steam_decode_test.cpp)

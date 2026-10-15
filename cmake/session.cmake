# session module: platform-independent session layer (docs/session.md). Needs the server and hooks_net modules
# (hooks::net::NetState, hooks::set_party_session_active), which pull in hooks_world/hooks_params.
foreach(_dep server hooks_net)
  if(NOT "${_dep}" IN_LIST NRSC_MODULES)
    message(FATAL_ERROR "session module requires the '${_dep}' module in NRSC_MODULES")
  endif()
endforeach()
nrsc_add_module(
  SOURCES src/nrsc/session/join_flow.cpp src/nrsc/session/lobby_lifecycle.cpp src/nrsc/session/frame_task.cpp
          src/nrsc/session/player_sync.cpp
          src/nrsc/session/party.cpp src/nrsc/session/session.cpp
  TESTS tests/session/session_test.cpp)

# session module: platform-independent session layer (docs/session.md). Needs the server and hooks_net modules
# (hooks::net::NetState, hooks::set_party_session_active), which pull in hooks_world/hooks_params.
foreach(_dep server hooks_net)
  if(NOT "${_dep}" IN_LIST NRSC_MODULES)
    message(FATAL_ERROR "session module requires the '${_dep}' module in NRSC_MODULES")
  endif()
endforeach()
target_sources(nrsc_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/session/join_flow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/session/lobby_lifecycle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/session/frame_task.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/session/party.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/session/session.cpp
)
if(TARGET nrsc_tests)
target_sources(nrsc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/session/session_test.cpp)
endif()

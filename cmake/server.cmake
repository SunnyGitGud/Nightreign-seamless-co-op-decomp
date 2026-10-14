# server module: nrsc\mod\server_manager\server_manager.cpp (Steam lobby, P2P messaging, leaderboard; no real Steam SDK).
# include()d from the top-level CMakeLists after nrsc_core and nrsc_tests exist, so paths are source-dir relative.
target_sources(nrsc_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/server/wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/server/peer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/server/lobby.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/server/message_bus.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/server/leaderboard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/server/task_timer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/server/callbacks.cpp
)
if(TARGET nrsc_tests)
target_sources(nrsc_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/server/server_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/server/callbacks_test.cpp
)
endif()

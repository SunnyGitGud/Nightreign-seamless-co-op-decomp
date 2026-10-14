# server module: nrsc\mod\server_manager\server_manager.cpp (Steam lobby, P2P messaging, leaderboard; no real Steam SDK).
nrsc_add_module(
  SOURCES src/nrsc/server/wire.cpp src/nrsc/server/peer.cpp src/nrsc/server/lobby.cpp src/nrsc/server/message_bus.cpp
          src/nrsc/server/leaderboard.cpp src/nrsc/server/task_timer.cpp src/nrsc/server/callbacks.cpp
  TESTS tests/server/server_test.cpp tests/server/callbacks_test.cpp)

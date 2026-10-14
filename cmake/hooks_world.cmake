# hooks_world module (docs/plan_gaps.md phase 2, work package 8): the world group (party size 3 -> 6, conditions, DLRF capture,
# SignIn auto-call).
nrsc_add_module(
  SOURCES src/nrsc/hooks/world/detours.cpp src/nrsc/hooks/world/wiring.cpp src/nrsc/hooks/world/world.cpp
  TESTS tests/hooks/world_test.cpp tests/hooks/world_wiring_test.cpp)

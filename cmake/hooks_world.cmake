# hooks_world module (docs/plan_gaps.md phase 2, work package 8): the world group (party size 3 -> 6, conditions, DLRF capture,
# SignIn auto-call).
target_sources(nrsc_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/hooks/world/detours.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/hooks/world/wiring.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/hooks/world/world.cpp
)
if(TARGET nrsc_tests)
  target_sources(nrsc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/hooks/world_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/tests/hooks/world_wiring_test.cpp)
endif()

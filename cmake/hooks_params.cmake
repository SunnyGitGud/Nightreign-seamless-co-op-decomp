# hooks_params module (WP9): params group (hooks.json #2-#5, #7, #10, #11, P3).
target_sources(nrsc_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/hooks/params/params_group.cpp)
if(TARGET nrsc_tests)
  target_sources(nrsc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/hooks/params_test.cpp)
endif()

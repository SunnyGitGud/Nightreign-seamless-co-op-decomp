# hooks_net module (WP10): net group (hooks.json #8, #9, #12-#16, #18, #19, P1, P2, P4, P9).
# The memory-block locator (docs/wiring_net.md) lives here because it needs the signatures module.
target_sources(nrsc_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/hooks/net/net_group.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/dantelion/block_table_locator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/hooks/session_word.cpp)
if(TARGET nrsc_tests)
  target_sources(nrsc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/hooks/net_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/hooks/real_binary_hooks_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/hooks/net_wire_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/hooks/session_word_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/dantelion/block_table_locator_test.cpp)
endif()

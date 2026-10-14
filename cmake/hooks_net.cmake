# hooks_net module (WP10): net group (hooks.json #8, #9, #12-#16, #18, #19, P1, P2, P4, P9).
# The memory-block locator (docs/wiring_net.md) lives here because it needs the signatures module.
nrsc_add_module(
  SOURCES src/nrsc/hooks/net/net_group.cpp src/nrsc/dantelion/block_table_locator.cpp src/nrsc/hooks/session_word.cpp
  TESTS tests/hooks/net_test.cpp tests/hooks/real_binary_hooks_test.cpp tests/hooks/net_wire_test.cpp
        tests/hooks/session_word_test.cpp tests/dantelion/block_table_locator_test.cpp)

# signatures module: nrsc\signatures\signatures.cpp (pattern parser, scanner, resolver, RTTI lookup, table).
nrsc_add_module(
  SOURCES src/nrsc/signatures/pattern.cpp src/nrsc/signatures/module_image.cpp src/nrsc/signatures/resolver.cpp
          src/nrsc/signatures/signature_table.cpp
  TESTS tests/signatures/signatures_test.cpp)
if(TARGET nrsc_tests)
  # The evidence test reads evidence/signatures/signatures.json (and the optional real-exe test compares against it).
  target_link_libraries(nrsc_tests PRIVATE nlohmann_json::nlohmann_json)
  target_compile_definitions(nrsc_tests PRIVATE NRSC_EVIDENCE_DIR="${PROJECT_SOURCE_DIR}/evidence")
endif()

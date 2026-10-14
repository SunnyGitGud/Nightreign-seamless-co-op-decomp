# signatures module: nrsc\signatures\signatures.cpp (pattern parser, scanner, resolver, RTTI lookup, table).
# include()d from the top-level CMakeLists after nrsc_core and nrsc_tests exist, so paths are source-dir relative.
target_sources(nrsc_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/signatures/pattern.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/signatures/module_image.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/signatures/resolver.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nrsc/signatures/signature_table.cpp
)
if(TARGET nrsc_tests)
target_sources(nrsc_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/signatures/signatures_test.cpp
)
# The evidence test reads evidence/signatures/signatures.json (and the optional real-exe test compares against it).
target_link_libraries(nrsc_tests PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(nrsc_tests PRIVATE NRSC_EVIDENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/evidence")
endif()

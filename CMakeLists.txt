cmake_minimum_required(VERSION 3.20)
project(nrsc_reconstruction LANGUAGES CXX)

# Readable reconstruction of the statically recoverable, self-contained parts of nrsc.dll
# (Seamless Co-op for Elden Ring Nightreign). See README.md and docs/.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(nrsc_core
  src/nrsc/config/ini_parser.cpp
  src/nrsc/config/ini_reader.cpp
  src/nrsc/config/settings.cpp
  src/nrsc/locale/message_repository.cpp
  src/nrsc/params/param_file.cpp
  src/nrsc/params/regulation_manager.cpp
  src/nrsc/cs/world_chr_man.cpp
  src/nrsc/cs/bloodstain_man.cpp
  src/nrsc/cs/auto_control_api.cpp
  src/nrsc/dantelion/memory.cpp
)
target_include_directories(nrsc_core PUBLIC src)
option(NRSC_BUILD_DLL "Build the loadable nrsc.dll (cmake/dll.cmake; needs the clang-cl/xwin toolchain)" OFF)
if(CMAKE_CROSSCOMPILING)
  set(_nrsc_tests_default OFF)
else()
  set(_nrsc_tests_default ON)
endif()
option(NRSC_BUILD_TESTS "Build nrsc_tests (GTest)" ${_nrsc_tests_default})

if(CMAKE_CROSSCOMPILING)
  # Header-only nlohmann_json from the host, exposed through a directory that holds only that one symlink
  # (never all of /usr/include, which would shadow the Windows SDK headers).
  set(_nlohmann_inc "${CMAKE_BINARY_DIR}/nlohmann_include")
  file(MAKE_DIRECTORY "${_nlohmann_inc}")
  if(NOT EXISTS "${_nlohmann_inc}/nlohmann")
    file(CREATE_LINK "/usr/include/nlohmann" "${_nlohmann_inc}/nlohmann" SYMBOLIC)
  endif()
  add_library(nrsc_nlohmann_json INTERFACE)
  target_include_directories(nrsc_nlohmann_json SYSTEM INTERFACE "${_nlohmann_inc}")
  add_library(nlohmann_json::nlohmann_json ALIAS nrsc_nlohmann_json)
else()
  find_package(nlohmann_json 3.11 REQUIRED)
endif()
target_link_libraries(nrsc_core PRIVATE nlohmann_json::nlohmann_json)

if(NRSC_BUILD_TESTS)
enable_testing()
find_package(GTest REQUIRED)

add_executable(nrsc_tests
  tests/config/ini_parser_test.cpp
  tests/config/ini_reader_test.cpp
  tests/config/settings_test.cpp
  tests/locale/message_repository_test.cpp
  tests/params/param_file_test.cpp
  tests/params/regulation_manager_test.cpp
  tests/cs/cs_test.cpp
  tests/dantelion/memory_test.cpp
)
target_link_libraries(nrsc_tests PRIVATE nrsc_core GTest::gtest_main)
target_compile_definitions(nrsc_tests PRIVATE
  NRSC_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference")
endif()

# Modules added in the gap-closing pass (docs/plan_gaps.md). Each fragment adds its own sources to nrsc_core and
# nrsc_tests with target_sources(). NRSC_MODULES selects the fragments (default: all), so a module can be built alone.
set(NRSC_MODULES "signatures;hooks;server;hooks_world;hooks_params;hooks_net;steam_adapter;session" CACHE STRING "Optional reconstruction modules to build")
foreach(module IN LISTS NRSC_MODULES)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/${module}.cmake)
endforeach()

if(TARGET nrsc_tests)
  include(GoogleTest)
  gtest_discover_tests(nrsc_tests)
endif()

if(NRSC_BUILD_DLL)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dll.cmake)
endif()

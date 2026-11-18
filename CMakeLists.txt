cmake_minimum_required(VERSION 3.25)
project(schemasync LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SCHEMASYNC_WERROR "Treat warnings as errors" OFF)

find_package(PostgreSQL REQUIRED)
include(cmake/Dependencies.cmake)

add_library(schemasync_warnings INTERFACE)
if(MSVC)
  target_compile_options(schemasync_warnings INTERFACE /W4 /permissive- /utf-8 $<$<BOOL:${SCHEMASYNC_WERROR}>:/WX>)
  target_compile_definitions(schemasync_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(schemasync_warnings INTERFACE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers
    $<$<BOOL:${SCHEMASYNC_WERROR}>:-Werror>)
endif()

add_library(schemasync_core STATIC
  src/db.cpp src/normalize.cpp src/parse.cpp src/introspect.cpp
  src/diff.cpp src/planner.cpp src/emit.cpp src/sync.cpp)
target_include_directories(schemasync_core PUBLIC src)
target_link_libraries(schemasync_core
  PUBLIC PostgreSQL::PostgreSQL
  PRIVATE schemasync_warnings pg_query nlohmann_json::nlohmann_json)

add_executable(schemasync src/main.cpp)
target_link_libraries(schemasync PRIVATE schemasync_warnings schemasync_core CLI11::CLI11)

enable_testing()
add_executable(schemasync_tests
  tests/unit_parse.cpp tests/unit_normalize.cpp tests/unit_diff.cpp tests/unit_planner.cpp tests/unit_emit.cpp
  tests/integration_db.cpp tests/integration_introspect.cpp tests/integration_convergence.cpp)
target_link_libraries(schemasync_tests PRIVATE
  schemasync_warnings schemasync_core pg_query nlohmann_json::nlohmann_json GTest::gmock GTest::gtest_main)
target_compile_definitions(schemasync_tests PRIVATE SCHEMASYNC_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
include(GoogleTest)
gtest_discover_tests(schemasync_tests)

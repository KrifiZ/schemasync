cmake_minimum_required(VERSION 3.25)
project(schemasync LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SCHEMASYNC_WERROR "Treat warnings as errors" OFF)

find_package(PostgreSQL REQUIRED)
# ponytail: vcpkg's static protobuf-c config links absl:: targets without finding absl; drop once the port is fixed
find_package(absl CONFIG REQUIRED)
find_package(unofficial-libpg-query CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_library(schemasync_warnings INTERFACE)
if(MSVC)
  target_compile_options(schemasync_warnings INTERFACE /W4 /permissive- /utf-8 $<$<BOOL:${SCHEMASYNC_WERROR}>:/WX>)
  target_compile_definitions(schemasync_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(schemasync_warnings INTERFACE -Wall -Wextra -Wpedantic $<$<BOOL:${SCHEMASYNC_WERROR}>:-Werror>)
endif()

add_executable(schemasync src/main.cpp)
target_link_libraries(schemasync PRIVATE schemasync_warnings CLI11::CLI11)

enable_testing()
add_executable(schemasync_tests tests/unit_parse.cpp)
target_link_libraries(schemasync_tests PRIVATE
  schemasync_warnings unofficial::libpg-query::libpg-query nlohmann_json::nlohmann_json GTest::gtest_main)
target_compile_definitions(schemasync_tests PRIVATE SCHEMASYNC_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
include(GoogleTest)
gtest_discover_tests(schemasync_tests)

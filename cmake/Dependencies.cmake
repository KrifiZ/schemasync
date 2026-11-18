include(FetchContent)

FetchContent_Declare(cli11
  URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.6.1.tar.gz
  URL_HASH SHA256=377691f3fac2b340f12a2f79f523c780564578ba3d6eaf5238e9f35895d5ba95)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
FetchContent_Declare(libpg_query
  URL https://github.com/pganalyze/libpg_query/archive/refs/tags/18.1.0.tar.gz
  URL_HASH SHA256=2d3486cf6a9d3955b53e66235db39d62b54216c820cd392ab66dc842c5b1316d)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cli11 json googletest libpg_query)

# libpg_query ships only Makefiles; these sources and flags mirror its Makefile / Makefile.msvc.
set(PGQ ${libpg_query_SOURCE_DIR})
file(GLOB PGQ_SOURCES ${PGQ}/src/*.c ${PGQ}/src/postgres/*.c)
add_library(pg_query STATIC ${PGQ_SOURCES}
  ${PGQ}/vendor/xxhash/xxhash.c
  ${PGQ}/vendor/upb/upb.c
  ${PGQ}/vendor/upb/third_party/utf8_range/utf8_range.c
  ${PGQ}/protobuf/pg_query.upb_minitable.c)
target_include_directories(pg_query SYSTEM PUBLIC ${PGQ})
target_include_directories(pg_query PRIVATE
  ${PGQ}/vendor ${PGQ}/vendor/upb ${PGQ}/vendor/upb/third_party/utf8_range
  ${PGQ}/src/include ${PGQ}/src/postgres/include)
if(MSVC)
  target_include_directories(pg_query PRIVATE
    ${PGQ}/src/postgres/include/port/win32 ${PGQ}/src/postgres/include/port/win32_msvc)
  target_compile_options(pg_query PRIVATE /w)
else()
  target_compile_options(pg_query PRIVATE -w -fno-strict-aliasing -fwrapv)
  find_package(Threads REQUIRED)
  target_link_libraries(pg_query PUBLIC Threads::Threads)
endif()

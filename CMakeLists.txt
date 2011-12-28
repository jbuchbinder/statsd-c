cmake_minimum_required (VERSION 2.6)
project (statsd-c)
set(STATSD_VERSION 0.1)

include(CheckIncludeFiles)
include(CheckFunctionExists)
check_include_files(limits.h HAVE_LIMITS_H)
check_function_exists(vasprintf HAVE_VASPRINTF)

# Passing variables
configure_file (
  "${PROJECT_SOURCE_DIR}/src/config.h.in"
  "${PROJECT_BINARY_DIR}/config.h"
  )
include_directories("${PROJECT_BINARY_DIR}")

# Daemon binary
add_executable(statsd src/statsd.c src/queue.c src/serialize.c src/strings.c src/embeddedgmetric/embeddedgmetric.c src/embeddedgmetric/modp_numtoa.c src/json-c/arraylist.c src/json-c/debug.c src/json-c/json_object.c src/json-c/json_tokener.c src/json-c/json_util.c src/json-c/linkhash.c src/json-c/printbuf.c)
set(CMAKE_C_FLAGS "-Wno-format-security -Wno-int-to-pointer-cast -Isrc/json-c -Isrc/embeddedgmetric -fPIC -pthread")

# Client binary
add_executable(statsd_client src/statsd_client.c)


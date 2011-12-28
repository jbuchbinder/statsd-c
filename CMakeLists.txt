cmake_minimum_required ( VERSION 2.6 )
project ( statsd-c )

set ( PACKAGE "statsd-c" )
set ( STATSD_VERSION_MAJOR 0 )
set ( STATSD_VERSION_MINOR 1 )
set ( STATSD_VERSION_MICRO 0 )
set ( VERSION "${STATSD_VERSION_MAJOR}.${STATSD_VERSION_MINOR}.${STATSD_VERSION_MICRO}" )
set ( STATSD_VERSION "\"${VERSION}\"" )

include ( CheckIncludeFiles )
include ( CheckFunctionExists )
check_include_files ( limits.h HAVE_LIMITS_H )
check_include_files ( sys/types.h HAVE_SYS_TYPES_H )
check_include_files ( sys/time.h HAVE_SYS_TIME_H )
check_include_files ( time.h HAVE_TIME_H )
check_include_files ( sys/stat.h HAVE_SYS_STAT_H )
check_include_files ( sys/ioctl.h HAVE_SYS_IOCTL_H )
check_include_files ( fcntl.h HAVE_FCNTL_H )
check_include_files ( sys/socket.h HAVE_SYS_SOCKET_H )
check_include_files ( netinet/in.h HAVE_NETINET_IN_H )
check_include_files ( netinet/tcp.h HAVE_NETINET_TCP_H )
check_include_files ( arpa/inet.h HAVE_ARPA_INET_H )
check_include_files ( limits.h HAVE_LIMITS_H )
check_include_files ( unistd.h HAVE_UNISTD_H )
check_include_files ( semaphore.h HAVE_SEMAPHORE_H )
check_include_files ( netdb.h HAVE_NETDB_H )
check_include_files ( pthread.h HAVE_PTHREAD_H )
check_include_files ( signal.h HAVE_SIGNAL_H )
check_function_exists ( vasprintf HAVE_VASPRINTF )

# Platform specific options
if ( ${CMAKE_SYSTEM} MATCHES "Linux" )
	set ( CMAKE_C_FLAGS "-Wno-format-security -Wno-int-to-pointer-cast -Isrc/json-c -Isrc/embeddedgmetric -fPIC -pthread" )
endif ( ${CMAKE_SYSTEM} MATCHES "Linux" )

if ( ${CMAKE_SYSTEM} MATCHES "Darwin" )
	set ( CMAKE_C_FLAGS "-Wno-format-security -Wno-int-to-pointer-cast -Isrc/json-c -Isrc/embeddedgmetric -fPIC -pthread" )
endif ( ${CMAKE_SYSTEM} MATCHES "Darwin" )

if ( ${CMAKE_SYSTEM} MATCHES "OS2" )
	set ( CMAKE_C_FLAGS "-Wno-format-security -Wno-int-to-pointer-cast -Isrc/json-c -Isrc/embeddedgmetric -fPIC -pthread" )
endif ( ${CMAKE_SYSTEM} MATCHES "OS2" )

# Passing variables
configure_file (
  "${PROJECT_SOURCE_DIR}/src/config.h.in"
  "${PROJECT_BINARY_DIR}/config.h"
  )
include_directories("${PROJECT_BINARY_DIR}")

# Daemon binary
add_executable(statsd src/statsd.c src/queue.c src/serialize.c src/strings.c src/embeddedgmetric/embeddedgmetric.c src/embeddedgmetric/modp_numtoa.c src/json-c/arraylist.c src/json-c/debug.c src/json-c/json_object.c src/json-c/json_tokener.c src/json-c/json_util.c src/json-c/linkhash.c src/json-c/printbuf.c)

# Client binary
add_executable(statsd_client src/statsd_client.c)

# CPack support
set ( CPACK_PACKAGE_DESCRIPTION_SUMMARY "statsd-c" )
set ( CPACK_PACKAGE_VENDOR "https://github.com/jbuchbinder/statsd-c" )
set ( CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md" )
set ( CPACK_PACKAGE_VERSION_MAJOR ${STATSD_VERSION_MAJOR} )
set ( CPACK_PACKAGE_VERSION_MINOR ${STATSD_VERSION_MINOR} )
set ( CPACK_PACKAGE_VERSION_PATCH ${STATSD_VERSION_MICRO} )
set ( CPACK_PACKAGE_EXECUTABLES "statsd" "statsd" )

include ( InstallRequiredSystemLibraries )
set ( CPACK_GENERATOR STGZ;TGZ;TBZ2;ZIP )
set ( CPACK_PACKAGE_NAME ${PACKAGE} )
set ( CPACK_STRIP_FILES ON )

include ( CPack )


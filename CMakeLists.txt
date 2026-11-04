cmake_minimum_required(VERSION 3.20)
project(ntplite VERSION 0.1.0 DESCRIPTION "Transport-agnostic NTP-style time sync" LANGUAGES C)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(NTPLITE_IS_TOP_LEVEL ON)
else()
  set(NTPLITE_IS_TOP_LEVEL OFF)
endif()

if(NTPLITE_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(NTPLITE_BUILD_TESTS "Build the GoogleTest unit tests" ${NTPLITE_IS_TOP_LEVEL})
option(NTPLITE_SANITIZE    "Build the test binaries with ASan + UBSan" ON)

set(NTPLITE_MAX_RECV_ATTEMPTS "4" CACHE STRING
  "Replies read per sync while discarding stale ones (integer >= 1)")
if(NOT NTPLITE_MAX_RECV_ATTEMPTS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR
    "NTPLITE_MAX_RECV_ATTEMPTS must be a positive integer, got '${NTPLITE_MAX_RECV_ATTEMPTS}'")
endif()

function(ntplite_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
set(NTPLITE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ntplite_common.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ntplite_client.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ntplite_server.c)

add_library(ntplite STATIC ${NTPLITE_SOURCES})

# Namespaced alias — downstream code uses ntplite::ntplite whether the
# library is found via find_package() or added via add_subdirectory/FetchContent.
add_library(ntplite::ntplite ALIAS ntplite)

target_include_directories(ntplite
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)           # <-- install-tree path

target_compile_definitions(ntplite PUBLIC
  NTPLITE_MAX_RECV_ATTEMPTS=${NTPLITE_MAX_RECV_ATTEMPTS})
target_compile_features(ntplite PUBLIC c_std_99)
set_target_properties(ntplite PROPERTIES
  C_STANDARD          99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS        OFF)
ntplite_enable_warnings(ntplite)

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
include(GNUInstallDirs)         # populates CMAKE_INSTALL_LIBDIR / INCLUDEDIR / …
include(CMakePackageConfigHelpers)

# 1. Library archive + public headers
install(TARGETS ntplite
  EXPORT  ntpliteTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h")

# 2. Export set → ntpliteTargets.cmake
#    Goes in  <prefix>/lib/cmake/ntplite/
install(EXPORT ntpliteTargets
  FILE        ntpliteTargets.cmake
  NAMESPACE   ntplite::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ntplite)

# 3. Config file  (ntpliteConfig.cmake)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ntpliteConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ntpliteConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ntplite)

# 4. Version file  (ntpliteConfigVersion.cmake)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ntpliteConfigVersion.cmake
  VERSION       ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ntpliteConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ntpliteConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ntplite)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NTPLITE_BUILD_TESTS)
  enable_language(CXX)
  enable_testing()
  add_subdirectory(test)
endif()
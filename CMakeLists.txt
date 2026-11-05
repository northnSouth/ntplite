cmake_minimum_required(VERSION 3.20)
project(ntplite VERSION 0.1.0 DESCRIPTION "Transport-agnostic NTP-style time sync" LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# Top-level detection. As a subproject (add_subdirectory / FetchContent) we
# must not change the consumer's build type, build our tests, sanitizer-
# instrument the archive they link against, or add install rules.
# ---------------------------------------------------------------------------
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(NTPLITE_IS_TOP_LEVEL ON)
else()
    set(NTPLITE_IS_TOP_LEVEL OFF)
endif()

get_property(_ntplite_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NTPLITE_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    if(NOT _ntplite_multi_config AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

# ---------------------------------------------------------------------------
# Options. Tests default ON only for a top-level Debug build.
# ---------------------------------------------------------------------------
if(NTPLITE_IS_TOP_LEVEL AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_ntplite_tests_default ON)
else()
    set(_ntplite_tests_default OFF)
endif()

option(NTPLITE_BUILD_TESTS "Build the GoogleTest unit tests"                     ${_ntplite_tests_default})
option(NTPLITE_SANITIZE    "Build library + tests with ASan/UBSan (tests only)" ON)
option(NTPLITE_INSTALL     "Generate install/export rules"                       ${NTPLITE_IS_TOP_LEVEL})

if(NTPLITE_IS_TOP_LEVEL)
    message(STATUS "ntplite: build type  = ${CMAKE_BUILD_TYPE}")
endif()
message(STATUS "ntplite: build tests = ${NTPLITE_BUILD_TESTS}")

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
add_library(ntplite STATIC
    src/ntplite_common.c
    src/ntplite_client.c
    src/ntplite_server.c)

# Same name in-tree and installed (NAMESPACE ntplite:: + target ntplite).
add_library(ntplite::ntplite ALIAS ntplite)

target_include_directories(ntplite PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(ntplite PUBLIC c_std_99)
set_target_properties(ntplite PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(ntplite PRIVATE /W4)
else()
    target_compile_options(ntplite PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Sanitizers — on the library itself so code under test is instrumented.
# PRIVATE so flags never leak into the exported target; test/CMakeLists.txt
# applies the same flags to the test executable.
# ---------------------------------------------------------------------------
set(NTPLITE_SANITIZE_ACTIVE OFF)
if(NTPLITE_BUILD_TESTS AND NTPLITE_SANITIZE)
    if(MSVC)
        set(NTPLITE_SAN_FLAGS      /fsanitize=address)
        set(NTPLITE_SAN_LINK_FLAGS "")
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set(NTPLITE_SAN_FLAGS
            -fsanitize=address,undefined
            -fno-sanitize-recover=undefined     # UB fails the test instead of printing
            -fno-omit-frame-pointer)
        set(NTPLITE_SAN_LINK_FLAGS -fsanitize=address,undefined)
    else()
        message(WARNING "NTPLITE_SANITIZE: no sanitizer flags known for ${CMAKE_C_COMPILER_ID}")
    endif()

    if(DEFINED NTPLITE_SAN_FLAGS)
        set(NTPLITE_SANITIZE_ACTIVE ON)
        target_compile_options(ntplite PRIVATE ${NTPLITE_SAN_FLAGS})
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NTPLITE_BUILD_TESTS)
    enable_language(CXX)
    enable_testing()
    add_subdirectory(test)
endif()

# ---------------------------------------------------------------------------
# Install + export
#   find_package(ntplite) -> ntplite::ntplite
# Skipped for sanitizer builds — never install an instrumented archive.
# src/ntplite_internal.h is deliberately not installed.
# ---------------------------------------------------------------------------
if(NTPLITE_INSTALL AND NOT NTPLITE_SANITIZE_ACTIVE)
    set(_ntplite_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/ntplite)

    install(TARGETS ntplite
        EXPORT  ntpliteTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

    install(EXPORT ntpliteTargets
        FILE        ntpliteTargets.cmake
        NAMESPACE   ntplite::
        DESTINATION ${_ntplite_cmake_dir})

    configure_package_config_file(
        cmake/ntpliteConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/ntpliteConfig.cmake
        INSTALL_DESTINATION ${_ntplite_cmake_dir})

    # Pre-1.0: a minor bump may break the ABI, so only the same 0.MINOR matches.
    # Switch to SameMajorVersion at 1.0.0.
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/ntpliteConfigVersion.cmake
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ntpliteConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ntpliteConfigVersion.cmake
        DESTINATION ${_ntplite_cmake_dir})

elseif(NTPLITE_INSTALL AND NTPLITE_SANITIZE_ACTIVE)
    message(STATUS "ntplite: install skipped for sanitizer build")
endif()
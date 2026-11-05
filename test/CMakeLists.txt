# GoogleTest: use an installed one if present, otherwise fetch it.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
    FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

add_executable(ntplite_tests
    test_common.cpp
    test_server.cpp
    test_client.cpp)

target_compile_features(ntplite_tests PRIVATE cxx_std_14)
set_target_properties(ntplite_tests PROPERTIES CXX_EXTENSIONS OFF)

# Internal header (wire format, offset math) is reachable from tests only.
target_include_directories(ntplite_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ntplite_tests PRIVATE ntplite GTest::gtest_main)

# Lets a test check that the library's PUBLIC definition reached this target.
target_compile_definitions(ntplite_tests PRIVATE
    NTPLITE_EXPECTED_RECV_ATTEMPTS=${NTPLITE_MAX_RECV_ATTEMPTS})

if(MSVC)
    target_compile_options(ntplite_tests PRIVATE /W4)
else()
    target_compile_options(ntplite_tests PRIVATE -Wall -Wextra)
endif()

if(NTPLITE_SANITIZE_ACTIVE)
    target_compile_options(ntplite_tests PRIVATE ${NTPLITE_SAN_FLAGS})
    target_link_options(ntplite_tests    PRIVATE ${NTPLITE_SAN_LINK_FLAGS})
endif()

gtest_discover_tests(ntplite_tests
    PROPERTIES TIMEOUT 10
               ENVIRONMENT "UBSAN_OPTIONS=print_stacktrace=1")
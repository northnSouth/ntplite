# GoogleTest: use an installed one if present, otherwise fetch it.
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

# ---------------------------------------------------------------------------
# Sanitizers. Applied to a *copy* of the library built from the same sources,
# so the shipped `ntplite` target stays uninstrumented while the tests still
# catch bugs inside the library code.
# ---------------------------------------------------------------------------
add_library(ntplite_sanitizers INTERFACE)
if(NTPLITE_SANITIZE)
  if(MSVC)
    target_compile_options(ntplite_sanitizers INTERFACE /fsanitize=address)
  elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ntplite_sanitizers INTERFACE
      -fsanitize=address,undefined
      -fno-sanitize-recover=undefined   # UB fails the test instead of printing
      -fno-omit-frame-pointer)
    target_link_options(ntplite_sanitizers INTERFACE -fsanitize=address,undefined)
  else()
    message(WARNING "NTPLITE_SANITIZE: no sanitizer flags known for ${CMAKE_C_COMPILER_ID}")
  endif()
endif()

add_library(ntplite_san STATIC ${NTPLITE_SOURCES})
target_include_directories(ntplite_san PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(ntplite_san PUBLIC
  NTPLITE_MAX_RECV_ATTEMPTS=${NTPLITE_MAX_RECV_ATTEMPTS})
set_target_properties(ntplite_san PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(ntplite_san PUBLIC ntplite_sanitizers)
ntplite_enable_warnings(ntplite_san)

add_executable(ntplite_tests
  test_common.cpp
  test_server.cpp
  test_client.cpp)
set_target_properties(ntplite_tests PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(ntplite_tests PRIVATE ntplite_san GTest::gtest_main)
# The test checks that the library's PUBLIC definition reached the test target.
target_compile_definitions(ntplite_tests PRIVATE
  NTPLITE_EXPECTED_RECV_ATTEMPTS=${NTPLITE_MAX_RECV_ATTEMPTS})
if(MSVC)
  target_compile_options(ntplite_tests PRIVATE /W4)
else()
  target_compile_options(ntplite_tests PRIVATE -Wall -Wextra)
endif()

include(GoogleTest)
gtest_discover_tests(ntplite_tests
  PROPERTIES ENVIRONMENT "UBSAN_OPTIONS=print_stacktrace=1")
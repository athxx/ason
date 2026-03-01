cmake_minimum_required(VERSION 3.14)
project(ason_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Release by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Library
add_library(ason STATIC src/ason.c)
target_include_directories(ason PUBLIC include)

# Examples
add_executable(basic examples/basic.c)
target_link_libraries(basic ason m)

add_executable(complex_example examples/complex.c)
target_link_libraries(complex_example ason m)

add_executable(bench examples/bench.c)
target_link_libraries(bench ason m)

add_executable(pool_bench examples/pool_bench.c)
target_link_libraries(pool_bench ason m)

add_executable(cross_compat examples/cross_compat.c)
target_link_libraries(cross_compat ason m)

# Tests
add_executable(ason_test tests/ason_test.c)
target_link_libraries(ason_test ason m)

add_executable(cross_compat_test tests/cross_compat_test.c)
target_link_libraries(cross_compat_test ason m)

enable_testing()
add_test(NAME ason_test COMMAND ason_test)
add_test(NAME cross_compat_test COMMAND cross_compat_test)

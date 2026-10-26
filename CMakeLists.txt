cmake_minimum_required(VERSION 3.16)
project(cpp_backtesting_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BT_BUILD_TESTS "Build the GoogleTest suite (downloads GoogleTest at configure time)" ON)
option(BT_BUILD_BENCHMARKS "Build the runtime benchmark" ON)

add_library(bt_core
  src/csv.cpp
  src/portfolio.cpp
  src/strategies.cpp
  src/engine.cpp
  src/metrics.cpp
  src/report.cpp)
target_include_directories(bt_core PUBLIC include)
if(MSVC)
  target_compile_options(bt_core PRIVATE /W4)
else()
  target_compile_options(bt_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_executable(bt_run apps/bt_run.cpp)
target_link_libraries(bt_run PRIVATE bt_core)

if(BT_BUILD_BENCHMARKS)
  add_executable(bt_bench benchmarks/bench_engine.cpp)
  target_link_libraries(bt_bench PRIVATE bt_core)
endif()

if(BT_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(bt_tests
    tests/test_csv.cpp
    tests/test_portfolio.cpp
    tests/test_engine.cpp
    tests/test_metrics.cpp)
  target_link_libraries(bt_tests PRIVATE bt_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(bt_tests)
endif()

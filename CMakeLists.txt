cmake_minimum_required(VERSION 3.16)
project(minislot CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)  # keep debug symbols by default - this is a debugging demo too
endif()

add_compile_options(-Wall -Wextra -g)

add_library(minislot_core
  src/Message.cpp
  src/UdpSocket.cpp
  src/CmStateMachine.cpp
  src/CmtsScheduler.cpp
)
target_include_directories(minislot_core PUBLIC include)
target_link_libraries(minislot_core PUBLIC pthread)

add_executable(cmts src/cmts_main.cpp)
target_link_libraries(cmts PRIVATE minislot_core)

add_executable(cm src/cm_main.cpp)
target_link_libraries(cm PRIVATE minislot_core)

enable_testing()
find_package(GTest QUIET)
if(GTest_FOUND)
  add_executable(minislot_tests
    test/test_message.cpp
    test/test_cm_state_machine.cpp
    test/test_cmts_scheduler.cpp
  )
  target_link_libraries(minislot_tests PRIVATE minislot_core GTest::gtest GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(minislot_tests)
else()
  message(WARNING "GTest not found - unit tests will not be built. Install libgtest-dev.")
endif()

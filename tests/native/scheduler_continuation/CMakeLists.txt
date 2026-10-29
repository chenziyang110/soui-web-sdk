cmake_minimum_required(VERSION 3.24)
project(SouiSchedulerContinuation LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "This contract requires Windows SDK types and an MSVC-compatible compiler")
endif()
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT)
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
find_program(NODE_EXECUTABLE node REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/browser/SharedTimerWin.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/web_impl_win/TaskSchedulingPolicy.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/web_impl_win/TaskSchedulingIndex.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/web_impl_win/WebThreadImpl.cpp")
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND_ERROR_IS_FATAL ANY)
add_executable(scheduler_continuation_contract fixture.cpp)
target_compile_features(scheduler_continuation_contract PRIVATE cxx_std_17)
target_compile_options(scheduler_continuation_contract PRIVATE /W4 /WX /EHsc
  /source-charset:.936 /execution-charset:utf-8 /wd4505)
target_compile_definitions(scheduler_continuation_contract PRIVATE
  NDEBUG UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_include_directories(scheduler_continuation_contract PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
set_property(TARGET scheduler_continuation_contract PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
target_link_libraries(scheduler_continuation_contract PRIVATE winmm)
enable_testing()
add_test(NAME scheduler.continuation.contract COMMAND scheduler_continuation_contract)
set_tests_properties(scheduler.continuation.contract PROPERTIES TIMEOUT 10)
foreach(boundary IN ITEMS close-order callback-drain)
  add_test(NAME scheduler.deadline.${boundary} COMMAND scheduler_continuation_contract "--${boundary}")
  set_tests_properties(scheduler.deadline.${boundary} PROPERTIES TIMEOUT 10)
endforeach()

cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiHostFramePolicy LANGUAGES CXX)
  enable_testing()
endif()
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT)
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
find_program(NODE_EXECUTABLE node REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}/sdk/src/host/soui_miniblink_host.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/browser/WebPage.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/browser/WebPageImpl.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/wke/InputUiApi.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/wke/InputUiApi.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/include/miniblink/input_ui_api.h")
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(host_frame_policy_source_bytes
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
    "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" VERBATIM)
add_executable(soui_host_frame_policy_tests fixture.cpp)
add_dependencies(soui_host_frame_policy_tests host_frame_policy_source_bytes)
target_compile_features(soui_host_frame_policy_tests PRIVATE cxx_std_17)
target_include_directories(soui_host_frame_policy_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_test(NAME sdk.native.host-frame-policy COMMAND soui_host_frame_policy_tests)

if(MSVC)
  target_compile_options(soui_host_frame_policy_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
  set_property(TARGET soui_host_frame_policy_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else()
  target_compile_options(soui_host_frame_policy_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_tests_properties(sdk.native.host-frame-policy PROPERTIES TIMEOUT 15)

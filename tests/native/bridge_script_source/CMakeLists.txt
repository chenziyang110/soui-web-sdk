cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiBridgeScriptSource LANGUAGES CXX)
  enable_testing()
endif()
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT)
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
endif()
find_program(NODE_EXECUTABLE node REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/generate.cjs"
  "${CMAKE_CURRENT_LIST_DIR}/legacy-source.inc"
  "${CMAKE_CURRENT_LIST_DIR}/legacy-reference.json")
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate.cjs"
  --verify "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(bridge_script_source_reference
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate.cjs"
    --verify "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated" VERBATIM)
add_executable(soui_bridge_script_source_tests fixture.cpp)
add_dependencies(soui_bridge_script_source_tests bridge_script_source_reference)
target_compile_features(soui_bridge_script_source_tests PRIVATE cxx_std_17)
target_include_directories(soui_bridge_script_source_tests PRIVATE "${SOUI_DISTRIBUTION_ROOT}/sdk/src/host")
if(MSVC)
  target_compile_options(soui_bridge_script_source_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
  set_property(TARGET soui_bridge_script_source_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else()
  target_compile_options(soui_bridge_script_source_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME sdk.native.bridge-script-source COMMAND soui_bridge_script_source_tests)
set_tests_properties(sdk.native.bridge-script-source PROPERTIES TIMEOUT 15)

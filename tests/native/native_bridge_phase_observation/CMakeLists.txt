cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiNativeBridgePhaseObservation LANGUAGES CXX)
  enable_testing()
endif()
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT)
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
endif()
find_package(Threads REQUIRED)
find_program(NODE_EXECUTABLE node REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}/sdk/src/host/soui_miniblink_host.cpp")
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(native_bridge_phase_observation_source_bytes
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate.cjs"
    "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" VERBATIM)
add_executable(soui_native_bridge_phase_observation_tests fixture.cpp)
add_dependencies(soui_native_bridge_phase_observation_tests native_bridge_phase_observation_source_bytes)
target_compile_features(soui_native_bridge_phase_observation_tests PRIVATE cxx_std_17)
target_include_directories(soui_native_bridge_phase_observation_tests PRIVATE
  "${SOUI_DISTRIBUTION_ROOT}/sdk/src/host"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/include/json"
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(soui_native_bridge_phase_observation_tests PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(soui_native_bridge_phase_observation_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
  set_property(TARGET soui_native_bridge_phase_observation_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else()
  target_compile_options(soui_native_bridge_phase_observation_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME sdk.native.native-bridge-phase-observation COMMAND soui_native_bridge_phase_observation_tests)
set_tests_properties(sdk.native.native-bridge-phase-observation PROPERTIES TIMEOUT 15)

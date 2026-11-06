cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiFrameBufferCopySource LANGUAGES CXX)
  enable_testing()
endif()
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT)
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
find_program(NODE_EXECUTABLE node REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/LayerTreeHost.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/FrameBufferLeasePolicy.h")
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND_ERROR_IS_FATAL ANY)
add_executable(frame_buffer_copy_contract fixture.cpp)
target_compile_features(frame_buffer_copy_contract PRIVATE cxx_std_17)
target_include_directories(frame_buffer_copy_contract PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(MSVC)
  target_compile_options(frame_buffer_copy_contract PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
  target_compile_definitions(frame_buffer_copy_contract PRIVATE NDEBUG NOMINMAX)
  set_property(TARGET frame_buffer_copy_contract PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else()
  target_compile_options(frame_buffer_copy_contract PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME kernel.native.frame-buffer-copy-source COMMAND frame_buffer_copy_contract)
set_tests_properties(kernel.native.frame-buffer-copy-source PROPERTIES TIMEOUT 15)

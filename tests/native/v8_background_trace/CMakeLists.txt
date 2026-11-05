cmake_minimum_required(VERSION 3.21)
project(soui_v8_background_trace_contracts LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "V8 QueueUserWorkItem trace contracts require Windows")
endif()

get_filename_component(SOUI_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(MINIBLINK_SOURCE_ROOT "${SOUI_SOURCE_ROOT}/third_party/sources/miniblink")
set(TRACE_IMPLEMENTATION "${MINIBLINK_SOURCE_ROOT}/content/common/InputFrameTrace.cpp")

function(configure_v8_background_trace_target target)
  target_include_directories(${target} PRIVATE "${MINIBLINK_SOURCE_ROOT}")
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_definitions(${target} PRIVATE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
  target_compile_options(${target} PRIVATE /EHsc /utf-8 /W4 /U_DEBUG /UNDEBUG)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
endfunction()

# Run this executable first. It deliberately omits the scope and exits nonzero
# only when the real QueueUserWorkItem callback reproduces the missing producer.
add_executable(soui_v8_background_trace_missing_scope
  v8_background_trace_tests.cpp "${TRACE_IMPLEMENTATION}")
configure_v8_background_trace_target(soui_v8_background_trace_missing_scope)
target_compile_definitions(soui_v8_background_trace_missing_scope PRIVATE SOUI_V8_BACKGROUND_TRACE_NEGATIVE_CONTROL=1)

add_executable(soui_v8_background_trace_scope
  v8_background_trace_tests.cpp "${TRACE_IMPLEMENTATION}")
configure_v8_background_trace_target(soui_v8_background_trace_scope)

enable_testing()
add_test(NAME kernel.native.v8-background-trace-scope COMMAND soui_v8_background_trace_scope)
set_tests_properties(kernel.native.v8-background-trace-scope PROPERTIES TIMEOUT 30)

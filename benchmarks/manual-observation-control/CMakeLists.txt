cmake_minimum_required(VERSION 3.24)
project(SouiManualObservationControl LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Manual observation control requires Windows and an MSVC-compatible compiler")
endif()

# Standalone cold helper; deliberately outside the sealed protocol tool graph.
add_executable(ManualObservationControl
  "${CMAKE_CURRENT_LIST_DIR}/../protocol/ManualObservationControl.cpp")
target_compile_features(ManualObservationControl PRIVATE cxx_std_17)
target_compile_definitions(ManualObservationControl PRIVATE
  UNICODE _UNICODE WINVER=0x0601 _WIN32_WINNT=0x0601)
target_compile_options(ManualObservationControl PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
target_link_libraries(ManualObservationControl PRIVATE kernel32 user32 advapi32)
set_property(TARGET ManualObservationControl PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

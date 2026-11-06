cmake_minimum_required(VERSION 3.24)
project(SouiInputCoordinateEvidence LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Windows/MSVC required")
endif()
set(CMAKE_SUPPRESS_REGENERATION ON)
add_executable(BurstInputCoordinateEvidence
  project/benchmarks/protocol/burst-input.cpp
  project/benchmarks/protocol/burst-desktop-observer.cpp)
target_compile_features(BurstInputCoordinateEvidence PRIVATE cxx_std_17)
target_compile_options(BurstInputCoordinateEvidence PRIVATE /W4 /utf-8)
target_compile_definitions(BurstInputCoordinateEvidence PRIVATE WINVER=0x0601 _WIN32_WINNT=0x0601 NOMINMAX)
target_link_libraries(BurstInputCoordinateEvidence PRIVATE user32 gdi32)
set_property(TARGET BurstInputCoordinateEvidence PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

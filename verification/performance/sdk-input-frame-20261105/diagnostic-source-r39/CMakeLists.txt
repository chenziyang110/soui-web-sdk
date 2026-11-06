cmake_minimum_required(VERSION 3.24)
project(SouiCellFeedback LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Windows/MSVC required")
endif()
set(CMAKE_SUPPRESS_REGENERATION ON)
add_executable(BurstInputCellFeedback
  project/benchmarks/protocol/burst-input.cpp
  project/benchmarks/protocol/burst-desktop-observer.cpp)
target_compile_features(BurstInputCellFeedback PRIVATE cxx_std_17)
target_compile_options(BurstInputCellFeedback PRIVATE /W4 /utf-8)
target_compile_definitions(BurstInputCellFeedback PRIVATE WINVER=0x0601 _WIN32_WINNT=0x0601 NOMINMAX)
target_link_libraries(BurstInputCellFeedback PRIVATE user32 gdi32)
set_property(TARGET BurstInputCellFeedback PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

cmake_minimum_required(VERSION 3.24)
project(SouiScrollCoverageRegression LANGUAGES CXX)
enable_testing()
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Scroll coverage regressions require Windows and MSVC")
endif()
get_filename_component(SOUI_SCROLL_KERNEL_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/sources/miniblink" ABSOLUTE)
set(SOUI_SCROLL_CONTROLLER_SOURCE
  "${SOUI_SCROLL_KERNEL_ROOT}/cc/input/ScrollController.cpp" CACHE FILEPATH
  "Production ScrollController source; override only for a preserved baseline comparison")
add_executable(soui_scroll_coverage_tests scroll_coverage_tests.cpp
  "${SOUI_SCROLL_CONTROLLER_SOURCE}"
  "${SOUI_SCROLL_KERNEL_ROOT}/cc/trees/ScrollTreeSnapshot.cpp")
target_include_directories(soui_scroll_coverage_tests PRIVATE "${SOUI_SCROLL_KERNEL_ROOT}")
target_compile_features(soui_scroll_coverage_tests PRIVATE cxx_std_17)
target_compile_options(soui_scroll_coverage_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
# Keep this standalone regression executable self-contained for isolated guests.
set_property(TARGET soui_scroll_coverage_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
add_test(NAME kernel.native.scroll-coverage COMMAND soui_scroll_coverage_tests)
set_tests_properties(kernel.native.scroll-coverage PROPERTIES TIMEOUT 10)

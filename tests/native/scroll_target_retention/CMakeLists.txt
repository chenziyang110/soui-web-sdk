cmake_minimum_required(VERSION 3.24)
project(SouiScrollTargetRetentionRegression LANGUAGES CXX)
enable_testing()
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Scroll target regressions require Windows and MSVC")
endif()
get_filename_component(SOUI_SCROLL_KERNEL_DEFAULT_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/sources/miniblink" ABSOLUTE)
set(SOUI_SCROLL_KERNEL_ROOT "${SOUI_SCROLL_KERNEL_DEFAULT_ROOT}" CACHE PATH
  "Production kernel source and headers; override only for an explicit preserved baseline comparison")
option(SOUI_BUILD_SCROLL_CONTRACTION_DIAGNOSTIC
  "Build the unresolved same-epoch coverage contraction counterexample" OFF)
set(scroll_test_targets soui_scroll_target_retention_tests soui_scroll_target_boundary_tests)
if(SOUI_BUILD_SCROLL_CONTRACTION_DIAGNOSTIC)
  list(APPEND scroll_test_targets soui_scroll_target_coverage_contraction_tests)
endif()
foreach(target IN LISTS scroll_test_targets)
  if(target STREQUAL "soui_scroll_target_retention_tests")
    set(test_source accepted_target_retention_tests.cpp)
  else()
    set(test_source target_retention_boundary_tests.cpp)
  endif()
  add_executable(${target} ${test_source}
    "${SOUI_SCROLL_KERNEL_ROOT}/cc/input/ScrollController.cpp"
    "${SOUI_SCROLL_KERNEL_ROOT}/cc/trees/ScrollTreeSnapshot.cpp")
  target_include_directories(${target} PRIVATE "${SOUI_SCROLL_KERNEL_ROOT}")
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
endforeach()
if(SOUI_BUILD_SCROLL_CONTRACTION_DIAGNOSTIC)
  target_compile_definitions(soui_scroll_target_coverage_contraction_tests
    PRIVATE SOUI_SCROLL_CONTRACTION_DIAGNOSTIC=1)
  add_test(NAME kernel.native.scroll-coverage-contraction-diagnostic
    COMMAND soui_scroll_target_coverage_contraction_tests)
  set_tests_properties(kernel.native.scroll-coverage-contraction-diagnostic
    PROPERTIES TIMEOUT 10)
endif()
add_test(NAME kernel.native.scroll-target-retention COMMAND soui_scroll_target_retention_tests)
add_test(NAME kernel.native.scroll-target-boundaries COMMAND soui_scroll_target_boundary_tests)
set_tests_properties(kernel.native.scroll-target-retention kernel.native.scroll-target-boundaries
  PROPERTIES TIMEOUT 10)

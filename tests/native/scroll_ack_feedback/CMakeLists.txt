cmake_minimum_required(VERSION 3.24)
project(SouiScrollAckFeedbackRegression LANGUAGES CXX)
enable_testing()
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Scroll ACK regressions require Windows and MSVC")
endif()
get_filename_component(SOUI_SCROLL_KERNEL_DEFAULT_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/sources/miniblink" ABSOLUTE)
set(SOUI_SCROLL_KERNEL_ROOT "${SOUI_SCROLL_KERNEL_DEFAULT_ROOT}" CACHE PATH
  "Production kernel source and headers")
set(SOUI_ACK_BASELINE_WEBLAYER_SOURCE "" CACHE FILEPATH
  "Explicit frozen WebLayerImpl.cpp; empty builds the real current ACK helper")
add_executable(soui_scroll_ack_feedback_tests ack_feedback_tests.cpp
  "${SOUI_SCROLL_KERNEL_ROOT}/cc/input/ScrollController.cpp"
  "${SOUI_SCROLL_KERNEL_ROOT}/cc/trees/ScrollTreeSnapshot.cpp")
target_include_directories(soui_scroll_ack_feedback_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}" "${SOUI_SCROLL_KERNEL_ROOT}")
target_compile_features(soui_scroll_ack_feedback_tests PRIVATE cxx_std_17)
target_compile_options(soui_scroll_ack_feedback_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
set_property(TARGET soui_scroll_ack_feedback_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
if(SOUI_ACK_BASELINE_WEBLAYER_SOURCE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/extract_baseline_ack.py"
    "${SOUI_ACK_BASELINE_WEBLAYER_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/baseline_ack_methods.cpp"
    COMMAND_ERROR_IS_FATAL ANY)
  target_sources(soui_scroll_ack_feedback_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/baseline_ack_methods.cpp")
  target_compile_definitions(soui_scroll_ack_feedback_tests PRIVATE SOUI_ACK_BASELINE=1)
endif()
add_test(NAME kernel.native.scroll-ack-feedback COMMAND soui_scroll_ack_feedback_tests)
set_tests_properties(kernel.native.scroll-ack-feedback PROPERTIES TIMEOUT 10)

cmake_minimum_required(VERSION 3.24)
project(SouiProviderDeadline LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Provider deadline contracts require Windows and an MSVC-compatible compiler")
endif()
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT)
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
find_program(NODE_EXECUTABLE node REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/common/PaintLifetime.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/wke/wkeWebView.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/wke/InputUiApi.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/wke/InputUiApi.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/include/miniblink/input_ui_api.h")
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(provider_deadline_source_bytes
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
    "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" VERBATIM)
add_executable(provider_deadline_contract fixture.cpp)
add_dependencies(provider_deadline_contract provider_deadline_source_bytes)
target_compile_features(provider_deadline_contract PRIVATE cxx_std_17)
target_compile_options(provider_deadline_contract PRIVATE /W4 /WX /EHsc /utf-8 /wd4505)
target_compile_definitions(provider_deadline_contract PRIVATE
  NDEBUG UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_include_directories(provider_deadline_contract PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_property(TARGET provider_deadline_contract PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
enable_testing()
foreach(case IN ITEMS validation both-lanes finite-sweep waiting-reader delete-reentry legacy-shutdown partial-failure clock-loss abi)
  add_test(NAME provider.deadline.${case} COMMAND provider_deadline_contract "${case}")
  set_tests_properties(provider.deadline.${case} PROPERTIES TIMEOUT 10)
endforeach()

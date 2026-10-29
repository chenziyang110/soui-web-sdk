cmake_minimum_required(VERSION 3.24)
project(SouiCurrentTimeHotPath LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "This fixture requires Windows SDK types and an MSVC-compatible compiler")
endif()
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT)
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
if(NOT SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE)
  find_program(SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE NAMES powershell pwsh REQUIRED)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Project-Source.ps1"
  "${CMAKE_CURRENT_SOURCE_DIR}/CurrentTimeImpl.original.cpp.hex"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/web_impl_win/CurrentTimeImpl.cpp")
execute_process(COMMAND "${SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE}" -NoProfile -NonInteractive
  -File "${CMAKE_CURRENT_SOURCE_DIR}/Project-Source.ps1"
  -SourceRoot "${SOUI_DISTRIBUTION_ROOT}" -OutputRoot "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND_ERROR_IS_FATAL ANY)
add_executable(current_time_hot_path_contract fixture.cpp)
target_compile_features(current_time_hot_path_contract PRIVATE cxx_std_17)
target_compile_options(current_time_hot_path_contract PRIVATE /W4 /WX /EHsc
  /source-charset:utf-8 /execution-charset:utf-8)
target_compile_definitions(current_time_hot_path_contract PRIVATE
  NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_include_directories(current_time_hot_path_contract PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_property(TARGET current_time_hot_path_contract PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
enable_testing()
foreach(scenario IN ITEMS steady fallback backwards tick-wrap drift)
  add_test(NAME current-time.hot-path.${scenario} COMMAND current_time_hot_path_contract "--${scenario}")
  set_tests_properties(current-time.hot-path.${scenario} PROPERTIES TIMEOUT 10)
endforeach()

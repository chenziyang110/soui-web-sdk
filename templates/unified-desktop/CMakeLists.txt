cmake_minimum_required(VERSION 3.24)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "Minimum macOS deployment version")
endif()
project(@APP_NAME@ VERSION @APP_VERSION@ LANGUAGES CXX)

# Private diagnostic build only; never inherit a path from the installed SDK.
option(SOUI_APP_STARTUP_TRACE "Enable private macOS startup stage tracing" OFF)
set(SOUI_APP_STARTUP_TRACE_FILE "" CACHE STRING "Explicit absolute precreated startup trace file")
if(SOUI_APP_STARTUP_TRACE)
  if(NOT APPLE)
    message(FATAL_ERROR "SOUI_APP_STARTUP_TRACE requires a macOS target")
  endif()
  # CMake also treats '~' as absolute; open() does not expand a home directory.
  if(NOT IS_ABSOLUTE "${SOUI_APP_STARTUP_TRACE_FILE}" OR
      NOT SOUI_APP_STARTUP_TRACE_FILE MATCHES "^/")
    message(FATAL_ERROR "SOUI_APP_STARTUP_TRACE_FILE must be an explicit absolute path")
  endif()
  string(HEX "${SOUI_APP_STARTUP_TRACE_FILE}" _soui_app_trace_path_hex)
  if(SOUI_APP_STARTUP_TRACE_FILE MATCHES ";" OR
      _soui_app_trace_path_hex MATCHES "^([0-9a-f][0-9a-f])*(0[0-9a-f]|1[0-9a-f]|7f|c2[89][0-9a-f])")
    message(FATAL_ERROR "SOUI_APP_STARTUP_TRACE_FILE must not contain controls or semicolons")
  endif()
  string(REPLACE "\\" "\\\\" SOUI_STARTUP_TRACE_FILE_ESCAPED "${SOUI_APP_STARTUP_TRACE_FILE}")
  string(REPLACE "\"" "\\\"" SOUI_STARTUP_TRACE_FILE_ESCAPED "${SOUI_STARTUP_TRACE_FILE_ESCAPED}")
  file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/startup_trace_config.h" CONTENT [=[
#pragma once
#define SOUI_STARTUP_TRACE_FILE "@SOUI_STARTUP_TRACE_FILE_ESCAPED@"
]=] @ONLY)
endif()

find_package(SouiDesktop CONFIG REQUIRED)

add_executable(@APP_NAME@ main.cpp)
target_compile_features(@APP_NAME@ PRIVATE cxx_std_17)
target_link_libraries(@APP_NAME@ PRIVATE Soui::Desktop)
if(SOUI_APP_STARTUP_TRACE)
  target_compile_definitions(@APP_NAME@ PRIVATE SOUI_STARTUP_TRACE_ENABLED=1)
  target_include_directories(@APP_NAME@ PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(APPLE)
  enable_language(OBJCXX)
  target_sources(@APP_NAME@ PRIVATE startup_dialog_macos.mm)
  set_property(TARGET @APP_NAME@ PROPERTY OBJCXX_STANDARD 17)
  set_property(TARGET @APP_NAME@ PROPERTY OBJCXX_STANDARD_REQUIRED ON)
  target_compile_options(@APP_NAME@ PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc;-fobjc-arc-exceptions>")
  target_link_libraries(@APP_NAME@ PRIVATE "-framework AppKit")
else()
  target_sources(@APP_NAME@ PRIVATE startup_dialog.cpp)
  if(WIN32)
    target_link_libraries(@APP_NAME@ PRIVATE user32)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SOUI_APP_GTK REQUIRED IMPORTED_TARGET "gtk+-3.0>=3.24")
    target_link_libraries(@APP_NAME@ PRIVATE PkgConfig::SOUI_APP_GTK)
  endif()
endif()
if(MSVC)
  target_compile_options(@APP_NAME@ PRIVATE /utf-8)
  set_property(TARGET @APP_NAME@ PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
endif()
# Refresh owned assets, including source deletions; never adopt arbitrary output.
soui_sync_application_assets(TARGET @APP_NAME@ ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/web")

set(APP_STAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/staged-$<CONFIG>" CACHE STRING
  "New application staging directory (must not already exist)")
set(deployment_rpath)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND deployment_rpath USE_INSTALL_RPATH)
endif()
soui_stage_application(TARGET @APP_NAME@ NAME @APP_NAME@
  ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/web" OUTPUT "${APP_STAGE_DIRECTORY}"
  VERSION "@APP_VERSION@" IDENTIFIER "@APP_IDENTIFIER@" ${deployment_rpath})

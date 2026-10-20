cmake_minimum_required(VERSION 3.24)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "Minimum macOS deployment version")
endif()
project(@APP_NAME@ VERSION @APP_VERSION@ LANGUAGES CXX)
find_package(SouiDesktop CONFIG REQUIRED)

add_executable(@APP_NAME@ main.cpp)
target_compile_features(@APP_NAME@ PRIVATE cxx_std_17)
target_link_libraries(@APP_NAME@ PRIVATE Soui::Desktop)
if(MSVC)
  target_compile_options(@APP_NAME@ PRIVATE /utf-8)
  set_property(TARGET @APP_NAME@ PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
endif()
# This explicit target also refreshes assets after frontend-only edits.
add_custom_target(@APP_NAME@_assets
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/web"
    "$<TARGET_FILE_DIR:@APP_NAME@>/web"
  VERBATIM)
add_dependencies(@APP_NAME@ @APP_NAME@_assets)

set(APP_STAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/staged-$<CONFIG>" CACHE STRING
  "New application staging directory (must not already exist)")
soui_stage_application(TARGET @APP_NAME@ NAME @APP_NAME@
  ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/web" OUTPUT "${APP_STAGE_DIRECTORY}"
  VERSION "@APP_VERSION@" IDENTIFIER "@APP_IDENTIFIER@")

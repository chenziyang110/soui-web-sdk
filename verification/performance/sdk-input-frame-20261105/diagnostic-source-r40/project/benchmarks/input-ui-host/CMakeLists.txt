cmake_minimum_required(VERSION 3.24)
project(SouiInputUiHost LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Input/UI measurement host requires the matched Windows/MSVC SDK")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/sdk_version.cmake")
soui_input_ui_resolve_sdk()
set(SOUI_SYNC_HAS_BACKEND_FRAME_RATE_API "" CACHE STRING "Explicit frozen bridge pacing API capability:0 or1")
if(NOT SOUI_SYNC_HAS_BACKEND_FRAME_RATE_API MATCHES "^[01]$")
  message(FATAL_ERROR "Explicit SOUI_SYNC_HAS_BACKEND_FRAME_RATE_API=0/1 required")
endif()
if((SOUI_INPUT_UI_EFFECTIVE_SDK_VERSION STREQUAL "0.6.0" AND NOT SOUI_SYNC_HAS_BACKEND_FRAME_RATE_API STREQUAL "0") OR
   (SOUI_INPUT_UI_EFFECTIVE_SDK_VERSION STREQUAL "0.7.0-preview.4" AND NOT SOUI_SYNC_HAS_BACKEND_FRAME_RATE_API STREQUAL "1") OR
   (NOT SOUI_INPUT_UI_EFFECTIVE_SDK_VERSION STREQUAL "0.6.0" AND NOT SOUI_INPUT_UI_EFFECTIVE_SDK_VERSION STREQUAL "0.7.0-preview.4"))
  message(FATAL_ERROR "Selected frozen SDK release/capability pair differs")
endif()
set(SOUI_INPUT_UI_LEGACY_SUPPORT "" CACHE PATH "Isolated diagnostic-only support prepared for a pre-trace installed SDK")
add_executable(SouiInputUiHost main.cpp trace_capture.cpp)
target_link_libraries(SouiInputUiHost PRIVATE Soui::Sdk Soui::ActionJson advapi32 comctl32)
target_compile_features(SouiInputUiHost PRIVATE cxx_std_17)
target_compile_definitions(SouiInputUiHost PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_compile_definitions(SouiInputUiHost PRIVATE SOUI_SYNC_HAS_BACKEND_FRAME_RATE_API=${SOUI_SYNC_HAS_BACKEND_FRAME_RATE_API})
target_compile_options(SouiInputUiHost PRIVATE /W4 /utf-8 /U_DEBUG)
set_property(TARGET SouiInputUiHost PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
# Binding evidence for independent consumers; generation does not run the Host.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sdk-identity-$<CONFIG>.txt" CONTENT
  "raw=${SOUI_INPUT_UI_EFFECTIVE_SDK_VERSION}\npackage=${SOUI_INPUT_UI_PACKAGE_VERSION}\nkind=${SOUI_INPUT_UI_SDK_IDENTITY_KIND}\nheader=${SOUI_INPUT_UI_SDK_VERSION_HEADER}\nlibrary=$<TARGET_FILE:Soui::Sdk>\n")
# R35 never overlays legacy trace/TLS support.

# Private diagnostic witness uses the same installed SDK trace/TLS symbols.
set(SOUI_SYNC_INPUT_ENGINE_ADMISSION_ROOT "" CACHE PATH "Explicit frozen SDK-private host header directory")
if(NOT IS_ABSOLUTE "${SOUI_SYNC_INPUT_ENGINE_ADMISSION_ROOT}" OR NOT EXISTS "${SOUI_SYNC_INPUT_ENGINE_ADMISSION_ROOT}/engine_admission.h")
  message(FATAL_ERROR "Explicit matching private engine_admission.h required")
endif()
if(SOUI_INPUT_UI_LEGACY_SUPPORT)
  message(FATAL_ERROR "R35 must not compile a second trace implementation")
endif()
target_include_directories(SouiInputUiHost PRIVATE "${SOUI_SYNC_INPUT_ENGINE_ADMISSION_ROOT}")
target_link_options(SouiInputUiHost PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/sync-input-$<CONFIG>.map")

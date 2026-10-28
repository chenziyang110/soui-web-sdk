cmake_minimum_required(VERSION 3.24)
project(SouiInputUiHost LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Input/UI measurement host requires the matched Windows/MSVC SDK")
endif()
if(NOT TARGET Soui::Sdk)
  find_package(SouiSdk 0.5.0 EXACT CONFIG REQUIRED)
endif()
set(SOUI_INPUT_UI_LEGACY_SUPPORT "" CACHE PATH "Isolated diagnostic-only support prepared for a pre-trace installed SDK")
add_executable(SouiInputUiHost main.cpp trace_capture.cpp)
target_link_libraries(SouiInputUiHost PRIVATE Soui::Sdk)
target_compile_features(SouiInputUiHost PRIVATE cxx_std_17)
target_compile_definitions(SouiInputUiHost PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_compile_options(SouiInputUiHost PRIVATE /W4 /utf-8 /U_DEBUG)
set_property(TARGET SouiInputUiHost PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
if(SOUI_INPUT_UI_LEGACY_SUPPORT)
  get_target_property(_input_ui_imported Soui::Sdk IMPORTED)
  if(NOT _input_ui_imported)
    message(FATAL_ERROR "Legacy trace support requires the matching separately installed baseline SDK")
  endif()
  get_target_property(_input_ui_sdk_includes Soui::Sdk INTERFACE_INCLUDE_DIRECTORIES)
  foreach(_input_ui_include IN LISTS _input_ui_sdk_includes)
    if(EXISTS "${_input_ui_include}/soui/diagnostics/input_frame_trace.h")
      message(FATAL_ERROR "This SDK already owns trace symbols; do not overlay a second implementation")
    endif()
  endforeach()
  foreach(_input_ui_support_file IN ITEMS
      support-manifest.json sdk/include/soui/diagnostics/input_frame_trace.h
      sdk/include/soui/diagnostics/miniblink_trace_source.h
      sdk/src/runtime/diagnostics/input_frame_trace.cpp
      sdk/src/runtime/diagnostics/input_frame_trace_backend_validation.h
      sdk/src/runtime/diagnostics/miniblink_trace_source.cpp third_party/include/miniblink/input_ui_api.h)
    if(NOT EXISTS "${SOUI_INPUT_UI_LEGACY_SUPPORT}/${_input_ui_support_file}")
      message(FATAL_ERROR "Incomplete isolated legacy trace support")
    endif()
  endforeach()
  # The isolated include tree contains only diagnostics. Public Host/layout
  # headers continue to come from the matching baseline SDK installation.
  add_library(input_ui_measurement_support STATIC
    "${SOUI_INPUT_UI_LEGACY_SUPPORT}/sdk/src/runtime/diagnostics/input_frame_trace.cpp"
    "${SOUI_INPUT_UI_LEGACY_SUPPORT}/sdk/src/runtime/diagnostics/miniblink_trace_source.cpp")
  target_include_directories(input_ui_measurement_support PUBLIC "${SOUI_INPUT_UI_LEGACY_SUPPORT}/sdk/include")
  target_compile_features(input_ui_measurement_support PRIVATE cxx_std_17)
  target_compile_definitions(input_ui_measurement_support PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
  target_compile_options(input_ui_measurement_support PRIVATE /W4 /utf-8 /U_DEBUG)
  set_property(TARGET input_ui_measurement_support PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  target_link_libraries(SouiInputUiHost PRIVATE input_ui_measurement_support)
endif()

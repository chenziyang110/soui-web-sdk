cmake_minimum_required(VERSION 3.24)
project(ScratchRendererCorrectness LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "This isolated actual-WebGL fixture requires Windows and MSVC")
endif()
get_filename_component(SOUI_CORRECTNESS_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SOUI_WEBVIEW2_SDK_ROOT "" CACHE PATH "Verified Microsoft.Web.WebView2 package")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(correctness_arch x64)
else()
  set(correctness_arch x86)
endif()
find_path(CORRECTNESS_WEBVIEW2_INCLUDE WebView2.h
  PATHS "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/include" NO_DEFAULT_PATH REQUIRED)
find_library(CORRECTNESS_WEBVIEW2_LOADER WebView2LoaderStatic
  PATHS "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/${correctness_arch}" NO_DEFAULT_PATH REQUIRED)
file(READ "${SOUI_CORRECTNESS_SDK_ROOT}/sdk/renderers/webview2/native-preload.js" W_PRELOAD_TEMPLATE)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../scratch-webview2-baseline/preload.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/preload.h" @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/renderer-fixture.js" RENDERER_CORRECTNESS_FIXTURE)
string(FIND "${RENDERER_CORRECTNESS_FIXTURE}" ")SOUI_RC\"" delimiter)
if(NOT delimiter EQUAL -1)
  message(FATAL_ERROR "Fixed fixture contains its C++ literal delimiter")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/renderer-fixture.js"
  "${SOUI_CORRECTNESS_SDK_ROOT}/sdk/renderers/webview2/native-preload.js")
configure_file(fixture-inputs.h.in "${CMAKE_CURRENT_BINARY_DIR}/fixture-inputs.h" @ONLY)
add_executable(ScratchRendererCorrectness WIN32 main.cpp)
target_compile_features(ScratchRendererCorrectness PRIVATE cxx_std_17)
target_compile_definitions(ScratchRendererCorrectness PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_compile_options(ScratchRendererCorrectness PRIVATE /utf-8 /W4 /U_DEBUG /EHsc)
target_include_directories(ScratchRendererCorrectness PRIVATE "${CORRECTNESS_WEBVIEW2_INCLUDE}"
  "${CMAKE_CURRENT_BINARY_DIR}" "${SOUI_CORRECTNESS_SDK_ROOT}/sdk/include"
  "${SOUI_CORRECTNESS_SDK_ROOT}/third_party/include/json"
  "${SOUI_CORRECTNESS_SDK_ROOT}/sdk/renderers/webview2")
target_link_libraries(ScratchRendererCorrectness PRIVATE "${CORRECTNESS_WEBVIEW2_LOADER}"
  user32 gdi32 shell32 ole32 oleaut32 uuid advapi32 shlwapi version bcrypt)
set_target_properties(ScratchRendererCorrectness PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

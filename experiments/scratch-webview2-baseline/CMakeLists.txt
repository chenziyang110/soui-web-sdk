cmake_minimum_required(VERSION 3.24)
project(ScratchRawWebView2Baseline LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "This local comparison requires Windows and MSVC")
endif()
get_filename_component(SOUI_BASELINE_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SOUI_WEBVIEW2_SDK_ROOT "" CACHE PATH "Verified Microsoft.Web.WebView2 package")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(baseline_arch x64)
else()
  set(baseline_arch x86)
endif()
find_path(BASELINE_WEBVIEW2_INCLUDE WebView2.h
  PATHS "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/include" NO_DEFAULT_PATH REQUIRED)
find_library(BASELINE_WEBVIEW2_LOADER WebView2LoaderStatic
  PATHS "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/${baseline_arch}" NO_DEFAULT_PATH REQUIRED)
file(READ "${SOUI_BASELINE_SDK_ROOT}/sdk/renderers/webview2/native-preload.js" W_PRELOAD_TEMPLATE)
configure_file(preload.h.in "${CMAKE_CURRENT_BINARY_DIR}/preload.h" @ONLY)
add_executable(ScratchRawWebView2Baseline WIN32 main.cpp)
target_compile_features(ScratchRawWebView2Baseline PRIVATE cxx_std_17)
target_compile_definitions(ScratchRawWebView2Baseline PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_compile_options(ScratchRawWebView2Baseline PRIVATE /utf-8 /W4 /U_DEBUG /EHsc)
target_include_directories(ScratchRawWebView2Baseline PRIVATE
  "${BASELINE_WEBVIEW2_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}"
  "${SOUI_BASELINE_SDK_ROOT}/sdk/include" "${SOUI_BASELINE_SDK_ROOT}/third_party/include/json"
  "${SOUI_BASELINE_SDK_ROOT}/sdk/renderers/webview2")
# Header-only policy reuse; no SDK Host/adapter/Bridge worker is linked or instantiated.
target_link_libraries(ScratchRawWebView2Baseline PRIVATE "${BASELINE_WEBVIEW2_LOADER}"
  user32 gdi32 shell32 ole32 oleaut32 uuid advapi32 shlwapi version bcrypt)
set_target_properties(ScratchRawWebView2Baseline PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

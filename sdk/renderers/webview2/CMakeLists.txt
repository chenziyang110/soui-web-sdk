# Explicit opt-in; the base SDK never acquires a WebView2 loader dependency.
set(SOUI_WEBVIEW2_SDK_ROOT "" CACHE PATH "Verified Microsoft.Web.WebView2 package")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(webview2_arch x64)
else()
  set(webview2_arch x86)
endif()
find_path(SOUI_WEBVIEW2_INCLUDE WebView2.h PATHS "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/include" NO_DEFAULT_PATH REQUIRED)
find_library(SOUI_WEBVIEW2_LOADER WebView2LoaderStatic PATHS "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/${webview2_arch}" NO_DEFAULT_PATH REQUIRED)
foreach(license IN ITEMS LICENSE.txt NOTICE.txt)
  if(NOT EXISTS "${SOUI_WEBVIEW2_SDK_ROOT}/${license}")
    message(FATAL_ERROR "Missing WebView2 redistribution notice: ${license}")
  endif()
endforeach()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/native-preload.js" SOUI_WEBVIEW2_PRELOAD)
configure_file(webview2_preload.h.in "${CMAKE_CURRENT_BINARY_DIR}/webview2_preload.h" @ONLY)
add_library(soui_webview2 STATIC webview2_host.cpp)
add_library(Soui::WebView2 ALIAS soui_webview2)
set_target_properties(soui_webview2 PROPERTIES EXPORT_NAME WebView2 MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_features(soui_webview2 PUBLIC cxx_std_17)
target_compile_options(soui_webview2 PRIVATE /utf-8 /W4 /U_DEBUG /EHsc)
target_include_directories(soui_webview2 PRIVATE "${SOUI_WEBVIEW2_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/include/json")
target_link_libraries(soui_webview2 PUBLIC Soui::Sdk
  "$<BUILD_INTERFACE:${SOUI_WEBVIEW2_LOADER}>" "$<INSTALL_INTERFACE:Soui::WebView2Loader>"
  shlwapi version bcrypt)
install(TARGETS soui_webview2 EXPORT SouiSdkTargets ARCHIVE DESTINATION lib/$<CONFIG>)
install(FILES "${SOUI_WEBVIEW2_LOADER}" DESTINATION lib/webview2)
install(FILES "${SOUI_WEBVIEW2_SDK_ROOT}/LICENSE.txt" "${SOUI_WEBVIEW2_SDK_ROOT}/NOTICE.txt"
  dependency.json DESTINATION share/SouiSdk/webview2)

# Auto host is independently opt-in. The base SDK and existing WebView2-only
# consumers do not acquire its compatibility startup orchestration.
if(SOUI_BUILD_AUTO_HOST)
  add_library(soui_auto_host STATIC ../auto/auto_webview_host.cpp)
  add_library(Soui::AutoHost ALIAS soui_auto_host)
  set_target_properties(soui_auto_host PROPERTIES EXPORT_NAME AutoHost MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  target_compile_features(soui_auto_host PUBLIC cxx_std_17)
  target_compile_options(soui_auto_host PRIVATE /utf-8 /W4 /U_DEBUG /EHsc)
  target_include_directories(soui_auto_host PRIVATE "${SOUI_WEBVIEW2_INCLUDE}")
  target_link_libraries(soui_auto_host PUBLIC Soui::WebView2)
  install(TARGETS soui_auto_host EXPORT SouiSdkTargets ARCHIVE DESTINATION lib/$<CONFIG>)
endif()

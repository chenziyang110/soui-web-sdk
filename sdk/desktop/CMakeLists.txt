soui_require_capability(system_desktop)
add_library(soui_desktop STATIC ../src/desktop/application.cpp ../src/desktop/protocol.cpp
  ../src/desktop/assets.cpp ../src/desktop/asset_executor.cpp ../src/desktop/resource_server.cpp ../src/desktop/app_paths.cpp)
add_library(Soui::Desktop ALIAS soui_desktop)
set_target_properties(soui_desktop PROPERTIES EXPORT_NAME Desktop
  SOUI_APPLICATION_PLATFORM "${SOUI_TARGET_OS}" SOUI_APPLICATION_ARCH "${SOUI_TARGET_ARCH}"
  EXPORT_PROPERTIES "SOUI_APPLICATION_PLATFORM;SOUI_APPLICATION_ARCH")
# Build-tree provenance only; installed targets get relocatable paths in Config.
set_target_properties(soui_desktop PROPERTIES
  SOUI_APPLICATION_SDK_LICENSE "${SOUI_DISTRIBUTION_ROOT}/LICENSE.md"
  SOUI_APPLICATION_RUNTIME_NOTICES "${CMAKE_CURRENT_SOURCE_DIR}/../portable/THIRD_PARTY_NOTICES.md")
target_compile_features(soui_desktop PUBLIC cxx_std_17)
target_link_libraries(soui_desktop PUBLIC Soui::Runtime)
target_include_directories(soui_desktop PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>" "$<INSTALL_INTERFACE:include>")
target_include_directories(soui_desktop PRIVATE "${SOUI_DISTRIBUTION_ROOT}/third_party/include/json")
# Private diagnostic build only. Never export the switch or the task file path.
option(SOUI_DESKTOP_STARTUP_TRACE "Enable bounded macOS startup stage tracing" OFF)
set(SOUI_DESKTOP_STARTUP_TRACE_FILE "" CACHE STRING "Explicit precreated empty 0600 SDK task trace file")
if(SOUI_DESKTOP_STARTUP_TRACE)
  if(NOT SOUI_TARGET_OS STREQUAL "macos")
    message(FATAL_ERROR "Startup tracing is supported only for explicit macOS diagnostic builds")
  endif()
  if(NOT IS_ABSOLUTE "${SOUI_DESKTOP_STARTUP_TRACE_FILE}" OR
      NOT SOUI_DESKTOP_STARTUP_TRACE_FILE MATCHES "^/")
    message(FATAL_ERROR "Startup tracing requires an explicit absolute task file")
  endif()
  string(HEX "${SOUI_DESKTOP_STARTUP_TRACE_FILE}" _soui_desktop_trace_path_hex)
  if(SOUI_DESKTOP_STARTUP_TRACE_FILE MATCHES ";" OR
      _soui_desktop_trace_path_hex MATCHES "^([0-9a-f][0-9a-f])*(0[0-9a-f]|1[0-9a-f]|7f|c2[89][0-9a-f])")
    message(FATAL_ERROR "Startup tracing paths must not contain controls or semicolons")
  endif()
  string(REPLACE "\\" "\\\\" SOUI_STARTUP_TRACE_FILE_ESCAPED "${SOUI_DESKTOP_STARTUP_TRACE_FILE}")
  string(REPLACE "\"" "\\\"" SOUI_STARTUP_TRACE_FILE_ESCAPED "${SOUI_STARTUP_TRACE_FILE_ESCAPED}")
  configure_file(startup_trace_config.h.in "${CMAKE_CURRENT_BINARY_DIR}/startup_trace_config.h" @ONLY)
  target_compile_definitions(soui_desktop PRIVATE SOUI_STARTUP_TRACE_ENABLED=1)
  target_include_directories(soui_desktop PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
set(SOUI_DESKTOP_HAS_X11_SHORTCUTS FALSE)
if(SOUI_TARGET_OS STREQUAL "linux")
  set(SOUI_DESKTOP_WEBKIT_HELPER_DIR "" CACHE STRING
    "Optional actual target WebKitGTK PKGLIBEXECDIR; empty means unknown (no preflight)")
  if(NOT SOUI_DESKTOP_WEBKIT_HELPER_DIR STREQUAL "" AND
      NOT SOUI_DESKTOP_WEBKIT_HELPER_DIR MATCHES "^/")
    message(FATAL_ERROR "SOUI_DESKTOP_WEBKIT_HELPER_DIR must be an absolute target Linux path")
  endif()
  if(SOUI_DESKTOP_WEBKIT_HELPER_DIR MATCHES "[\n\r;]")
    message(FATAL_ERROR "SOUI_DESKTOP_WEBKIT_HELPER_DIR contains unsupported control/list characters")
  endif()
  string(REPLACE "\\" "\\\\" SOUI_DESKTOP_WEBKIT_HELPER_DIR_ESCAPED "${SOUI_DESKTOP_WEBKIT_HELPER_DIR}")
  string(REPLACE "\"" "\\\"" SOUI_DESKTOP_WEBKIT_HELPER_DIR_ESCAPED "${SOUI_DESKTOP_WEBKIT_HELPER_DIR_ESCAPED}")
  configure_file(webkit_runtime_config.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/webkit_runtime_config.h" @ONLY)
  target_include_directories(soui_desktop PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SOUI_WEBKIT REQUIRED IMPORTED_TARGET "webkit2gtk-4.1>=2.40" "gtk+-3.0>=3.24")
  target_sources(soui_desktop PRIVATE ../src/desktop/backends/linux_webkitgtk.cpp)
  target_link_libraries(soui_desktop PRIVATE PkgConfig::SOUI_WEBKIT)
  option(SOUI_DESKTOP_ENABLE_X11_SHORTCUTS "Build optional X11 global shortcut support when dependencies exist" ON)
  if(SOUI_DESKTOP_ENABLE_X11_SHORTCUTS)
    pkg_check_modules(SOUI_DESKTOP_X11 QUIET IMPORTED_TARGET "gdk-x11-3.0>=3.24" "x11")
    if(SOUI_DESKTOP_X11_FOUND)
      set(SOUI_DESKTOP_HAS_X11_SHORTCUTS TRUE)
      target_compile_definitions(soui_desktop PRIVATE SOUI_DESKTOP_X11_SHORTCUTS=1)
      target_link_libraries(soui_desktop PRIVATE PkgConfig::SOUI_DESKTOP_X11)
    else()
      message(STATUS "X11 shortcut dependencies unavailable: global shortcuts will report not_supported")
    endif()
  endif()
  set(SOUI_DESKTOP_BACKEND webkitgtk)
elseif(SOUI_TARGET_OS STREQUAL "macos")
  if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 12.0)
    message(FATAL_ERROR "The new WKWebView desktop backend requires macOS 12.0+ for explicit media permission denial.")
  endif()
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(SOUI_DESKTOP_MIN_MACOS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  else()
    set(SOUI_DESKTOP_MIN_MACOS 12.0)
  endif()
  enable_language(OBJCXX)
  set_target_properties(soui_desktop PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
  target_sources(soui_desktop PRIVATE ../src/desktop/backends/macos_wkwebview.mm)
  target_compile_options(soui_desktop PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
  target_compile_options(soui_desktop PRIVATE "-mmacosx-version-min=${SOUI_DESKTOP_MIN_MACOS}")
  target_link_libraries(soui_desktop PRIVATE "-framework AppKit" "-framework Foundation"
    "-framework WebKit" "-framework CoreFoundation" "-framework Carbon" "-framework UniformTypeIdentifiers")
  set(SOUI_DESKTOP_BACKEND wkwebview)
elseif(SOUI_TARGET_OS STREQUAL "windows")
  if(NOT MSVC OR SOUI_TARGET_ARCH STREQUAL "arm32")
    message(FATAL_ERROR "The new WebView2 desktop backend requires MSVC-compatible x86, x64 or ARM64; Windows ARM32 has no supported WebView2 distribution.")
  endif()
  set(SOUI_WEBVIEW2_SDK_ROOT "" CACHE PATH "Official Microsoft.Web.WebView2 SDK package root")
  find_path(SOUI_DESKTOP_WEBVIEW2_INCLUDE WebView2.h
    PATHS "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/include" NO_DEFAULT_PATH REQUIRED)
  find_library(SOUI_DESKTOP_WEBVIEW2_LOADER WebView2LoaderStatic
    PATHS "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/${SOUI_TARGET_ARCH}" NO_DEFAULT_PATH REQUIRED)
  # find_* cache entries can survive a package-root change. Never pair a loader
  # from one package with notices or headers from another.
  file(REAL_PATH "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/${SOUI_TARGET_ARCH}/WebView2LoaderStatic.lib" expected_loader)
  file(REAL_PATH "${SOUI_DESKTOP_WEBVIEW2_LOADER}" actual_loader)
  file(REAL_PATH "${SOUI_WEBVIEW2_SDK_ROOT}/build/native/include" expected_include)
  file(REAL_PATH "${SOUI_DESKTOP_WEBVIEW2_INCLUDE}" actual_include)
  if(NOT actual_loader STREQUAL expected_loader OR NOT actual_include STREQUAL expected_include)
    message(FATAL_ERROR "WebView2 cached loader/headers do not match SOUI_WEBVIEW2_SDK_ROOT; use a fresh build directory")
  endif()
  set_target_properties(soui_desktop PROPERTIES
    SOUI_APPLICATION_WEBVIEW2_LICENSE "${SOUI_WEBVIEW2_SDK_ROOT}/LICENSE.txt"
    SOUI_APPLICATION_WEBVIEW2_NOTICES "${SOUI_WEBVIEW2_SDK_ROOT}/NOTICE.txt")
  foreach(notice LICENSE.txt NOTICE.txt)
    if(NOT EXISTS "${SOUI_WEBVIEW2_SDK_ROOT}/${notice}")
      message(FATAL_ERROR "Official WebView2 package is missing ${notice}; provide the complete SDK.")
    endif()
  endforeach()
  target_sources(soui_desktop PRIVATE ../src/desktop/backends/windows_webview2.cpp)
  target_include_directories(soui_desktop PRIVATE "${SOUI_DESKTOP_WEBVIEW2_INCLUDE}")
  target_compile_options(soui_desktop PRIVATE /utf-8 /W4)
  target_compile_definitions(soui_desktop PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  set_property(TARGET soui_desktop PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  target_link_libraries(soui_desktop PRIVATE
    "$<BUILD_INTERFACE:${SOUI_DESKTOP_WEBVIEW2_LOADER}>"
    "$<INSTALL_INTERFACE:Soui::DesktopWebView2Loader>"
    ole32 shell32 user32 uuid version shlwapi ws2_32)
  install(FILES "${SOUI_DESKTOP_WEBVIEW2_LOADER}" DESTINATION lib/SouiDesktop/webview2)
  install(FILES "${SOUI_WEBVIEW2_SDK_ROOT}/LICENSE.txt" "${SOUI_WEBVIEW2_SDK_ROOT}/NOTICE.txt"
    DESTINATION share/SouiDesktop/webview2)
  set(SOUI_DESKTOP_BACKEND webview2)
else()
  message(FATAL_ERROR "No system-WebView backend exists for ${SOUI_TARGET_OS}.")
endif()
if(NOT MSVC)
  target_compile_options(soui_desktop PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS soui_desktop EXPORT SouiDesktopTargets ARCHIVE DESTINATION lib/$<CONFIG>)
install(FILES ../include/soui/desktop/app.h DESTINATION include/soui/desktop)
configure_package_config_file(SouiDesktopConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/SouiDesktopConfig.cmake" INSTALL_DESTINATION lib/cmake/SouiDesktop)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/SouiDesktopConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}" COMPATIBILITY ExactVersion)
install(EXPORT SouiDesktopTargets NAMESPACE Soui:: DESTINATION lib/cmake/SouiDesktop)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SouiDesktopConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/SouiDesktopConfigVersion.cmake" DESTINATION lib/cmake/SouiDesktop)

include("${CMAKE_CURRENT_SOURCE_DIR}/SouiDesktopApplication.cmake")
install(FILES SouiDesktopApplication.cmake DESTINATION lib/cmake/SouiDesktop)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/stage-desktop-app.cjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/sync-desktop-assets.cjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/desktop-preflight.cjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/verify-desktop-app.cjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/archive-desktop-app.cjs"
  DESTINATION share/SouiDesktop/tools)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/create-desktop-app.cjs"
  DESTINATION share/SouiDesktop/tools)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../templates/unified-desktop"
  DESTINATION share/SouiDesktop/templates)
install(FILES frontend/desktop.mjs frontend/desktop.d.mts
  DESTINATION share/SouiDesktop/frontend)

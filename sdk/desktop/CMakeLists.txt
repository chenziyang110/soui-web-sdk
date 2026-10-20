soui_require_capability(system_desktop)
add_library(soui_desktop STATIC ../src/desktop/application.cpp ../src/desktop/protocol.cpp
  ../src/desktop/assets.cpp ../src/desktop/asset_executor.cpp ../src/desktop/resource_server.cpp ../src/desktop/app_paths.cpp)
add_library(Soui::Desktop ALIAS soui_desktop)
set_target_properties(soui_desktop PROPERTIES EXPORT_NAME Desktop
  SOUI_APPLICATION_PLATFORM "${SOUI_TARGET_OS}" SOUI_APPLICATION_ARCH "${SOUI_TARGET_ARCH}"
  EXPORT_PROPERTIES "SOUI_APPLICATION_PLATFORM;SOUI_APPLICATION_ARCH")
target_compile_features(soui_desktop PUBLIC cxx_std_17)
target_link_libraries(soui_desktop PUBLIC Soui::Runtime)
target_include_directories(soui_desktop PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>" "$<INSTALL_INTERFACE:include>")
target_include_directories(soui_desktop PRIVATE "${SOUI_DISTRIBUTION_ROOT}/third_party/include/json")
if(SOUI_TARGET_OS STREQUAL "linux")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SOUI_WEBKIT REQUIRED IMPORTED_TARGET "webkit2gtk-4.1>=2.40" "gtk+-3.0>=3.24")
  target_sources(soui_desktop PRIVATE ../src/desktop/backends/linux_webkitgtk.cpp)
  target_link_libraries(soui_desktop PRIVATE PkgConfig::SOUI_WEBKIT)
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
    "-framework WebKit" "-framework CoreFoundation")
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
  DESTINATION share/SouiDesktop/tools)

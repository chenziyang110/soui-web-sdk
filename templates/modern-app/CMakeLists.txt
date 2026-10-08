if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
  project(SouiModernStarter LANGUAGES C CXX)
  if(NOT SOUI_BUILD_WEBVIEW2)
    message(FATAL_ERROR "Use tools/Build.ps1 -WithWebView2 for the modern template")
  endif()
  add_subdirectory(sdk)
endif()
add_executable(SouiModernStarter WIN32 app/main.cpp app/application.manifest)
target_link_libraries(SouiModernStarter PRIVATE Soui::WebView2)
target_include_directories(SouiModernStarter PRIVATE "${SOUI_DISTRIBUTION_ROOT}/third_party/include/json")
target_compile_options(SouiModernStarter PRIVATE /utf-8 /W4 /U_DEBUG)
set_target_properties(SouiModernStarter PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/SouiModernStarter")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(modern_arch x64)
else()
  set(modern_arch x86)
endif()
find_program(SOUI_MODERN_NODE node.exe REQUIRED)
file(GLOB_RECURSE modern_crt "${SOUI_DISTRIBUTION_ROOT}/third_party/prebuilt/${modern_arch}/runtime/*.dll")
add_custom_target(SouiModernStarter_resources ALL
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${modern_crt} "$<TARGET_FILE_DIR:SouiModernStarter>"
  COMMAND "${SOUI_MODERN_NODE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/deploy-resources.cjs"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/dist" "$<TARGET_FILE_DIR:SouiModernStarter>" "${SOUI_DISTRIBUTION_ROOT}"
  DEPENDS SouiModernStarter VERBATIM)

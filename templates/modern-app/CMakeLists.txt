if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
  project(SouiModernStarter LANGUAGES C CXX)
  if(NOT SOUI_BUILD_WEBVIEW2)
    message(FATAL_ERROR "Use tools/Build.ps1 -WithWebView2 for the modern template")
  endif()
  add_subdirectory(sdk)
endif()
add_executable(SouiModernStarter WIN32 app/main.cpp app/application.manifest)
target_link_libraries(SouiModernStarter PRIVATE Soui::WebView2 Soui::ActionJson)
target_compile_options(SouiModernStarter PRIVATE /utf-8 /W4 /U_DEBUG /EHsc)
set_target_properties(SouiModernStarter PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/SouiModernStarter")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(modern_arch x64)
else()
  set(modern_arch x86)
endif()
find_program(SOUI_MODERN_NODE node.exe REQUIRED)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/project.json")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/project.json")
endif()
execute_process(COMMAND "${SOUI_MODERN_NODE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/web-targets.cjs"
  output "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE modern_web_root OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE modern_web_result ERROR_VARIABLE modern_web_error)
if(NOT modern_web_result EQUAL 0)
  message(FATAL_ERROR "Invalid application Web targets: ${modern_web_error}")
endif()
add_custom_target(SouiModernStarter_actions_check ALL
  COMMAND "${SOUI_MODERN_NODE}" "${SOUI_DISTRIBUTION_ROOT}/tools/sdk/actiongen.cjs"
    --source "${CMAKE_CURRENT_SOURCE_DIR}/contracts/actions.json" --root "${CMAKE_CURRENT_SOURCE_DIR}" --check
  VERBATIM)
add_dependencies(SouiModernStarter SouiModernStarter_actions_check)
add_custom_target(SouiModernStarter_resources ALL
  COMMAND "${SOUI_MODERN_NODE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/runtime-deployment.cjs" deploy
    --sdk-root "${SOUI_DISTRIBUTION_ROOT}" --application "$<TARGET_FILE_DIR:SouiModernStarter>"
    --architecture "${modern_arch}" --backend webview2 --project-root "${CMAKE_CURRENT_SOURCE_DIR}" --adopt-legacy
  COMMAND "${SOUI_MODERN_NODE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/deploy-resources.cjs"
    "${modern_web_root}" "$<TARGET_FILE_DIR:SouiModernStarter>" "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS SouiModernStarter VERBATIM)

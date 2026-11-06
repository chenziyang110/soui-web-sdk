cmake_minimum_required(VERSION 3.24)
project(SouiScratch LANGUAGES C CXX)

get_filename_component(SCRATCH_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
file(STRINGS "${SCRATCH_SDK_ROOT}/VERSION" scratch_sdk_version LIMIT_COUNT 1)
if(NOT scratch_sdk_version MATCHES "^0\\.6\\.0($|-)")
  message(FATAL_ERROR "SouiScratch requires SOUI SDK 0.6.0")
endif()
set(SOUI_BUILD_WEBVIEW2 ON CACHE BOOL "Scratch uses the modern WebView2 backend" FORCE)
add_subdirectory("${SCRATCH_SDK_ROOT}/sdk" "${CMAKE_CURRENT_BINARY_DIR}/sdk")

add_executable(SouiScratch WIN32 app/main.cpp app/application.manifest)
target_link_libraries(SouiScratch PRIVATE Soui::WebView2 Soui::ActionJson comdlg32)
target_compile_options(SouiScratch PRIVATE /utf-8 /W4 /U_DEBUG /EHsc)
set_target_properties(SouiScratch PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/SouiScratch")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(scratch_arch x64)
else()
  set(scratch_arch x86)
endif()
find_program(SCRATCH_NODE node.exe REQUIRED)
add_custom_target(SouiScratch_resources ALL
  COMMAND "${SCRATCH_NODE}" "${SCRATCH_SDK_ROOT}/tools/internal/runtime-deployment.cjs" deploy
    --sdk-root "${SCRATCH_SDK_ROOT}" --application "$<TARGET_FILE_DIR:SouiScratch>"
    --architecture "${scratch_arch}" --backend webview2 --project-root "${CMAKE_CURRENT_SOURCE_DIR}" --adopt-legacy
  COMMAND "${SCRATCH_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/deploy.mjs"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/dist" "$<TARGET_FILE_DIR:SouiScratch>"
  DEPENDS SouiScratch VERBATIM)

add_executable(SouiScratch_export_contract EXCLUDE_FROM_ALL app/project_file_save_tests.cpp)
target_link_libraries(SouiScratch_export_contract PRIVATE Soui::Sdk Soui::ActionJson comdlg32)
target_compile_options(SouiScratch_export_contract PRIVATE /utf-8 /W4 /U_DEBUG /EHsc)
set_property(TARGET SouiScratch_export_contract PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

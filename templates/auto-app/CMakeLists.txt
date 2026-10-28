if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
  project(SouiAutoStarter LANGUAGES C CXX)
  if(NOT SOUI_BUILD_AUTO_HOST)
    message(FATAL_ERROR "Use tools/Build.ps1 -WithAutoHost for the auto template")
  endif()
  add_subdirectory(sdk)
endif()
add_executable(SouiAutoStarter WIN32 app/main.cpp app/application.manifest)
target_link_libraries(SouiAutoStarter PRIVATE Soui::AutoHost Soui::ActionJson)
target_compile_options(SouiAutoStarter PRIVATE /utf-8 /W4 /U_DEBUG /EHsc)
target_link_options(SouiAutoStarter PRIVATE /SUBSYSTEM:WINDOWS,6.01)
set_target_properties(SouiAutoStarter PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/SouiAutoStarter")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(auto_arch x64)
else()
  set(auto_arch x86)
endif()
find_program(SOUI_AUTO_NODE node.exe REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/project.json")
add_custom_target(SouiAutoStarter_actions_check ALL
  COMMAND "${SOUI_AUTO_NODE}" "${SOUI_DISTRIBUTION_ROOT}/tools/sdk/actiongen.cjs"
    --source "${CMAKE_CURRENT_SOURCE_DIR}/contracts/actions.json" --root "${CMAKE_CURRENT_SOURCE_DIR}" --check
  VERBATIM)
add_dependencies(SouiAutoStarter SouiAutoStarter_actions_check)
add_custom_target(SouiAutoStarter_resources ALL
  COMMAND "${SOUI_AUTO_NODE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/runtime-deployment.cjs" deploy
    --sdk-root "${SOUI_DISTRIBUTION_ROOT}" --application "$<TARGET_FILE_DIR:SouiAutoStarter>"
    --architecture "${auto_arch}" --backend auto --project-root "${CMAKE_CURRENT_SOURCE_DIR}" --adopt-legacy
  COMMAND "${SOUI_AUTO_NODE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/deploy-resources.cjs" auto
    "${CMAKE_CURRENT_SOURCE_DIR}" "$<TARGET_FILE_DIR:SouiAutoStarter>" "${SOUI_DISTRIBUTION_ROOT}"
  DEPENDS SouiAutoStarter VERBATIM)

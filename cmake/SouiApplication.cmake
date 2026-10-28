include_guard(GLOBAL)
function(soui_add_application target)
  cmake_parse_arguments(APP "" "ROOT;WEB_ROOT" "SOURCES" ${ARGN})
  if(NOT APP_ROOT)
    set(APP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  find_program(SOUI_NODE_EXECUTABLE node.exe REQUIRED)
  if(NOT APP_WEB_ROOT)
    if(EXISTS "${APP_ROOT}/project.json")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APP_ROOT}/project.json")
    endif()
    execute_process(COMMAND "${SOUI_NODE_EXECUTABLE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/web-targets.cjs"
      output "${APP_ROOT}" OUTPUT_VARIABLE APP_WEB_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE web_target_result ERROR_VARIABLE web_target_error)
    if(NOT web_target_result EQUAL 0)
      message(FATAL_ERROR "Invalid application Web targets: ${web_target_error}")
    endif()
  endif()
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(arch x64)
  else()
    set(arch x86)
  endif()
  set(dep "${SOUI_DISTRIBUTION_ROOT}/third_party")
  if(SOUI_NATIVE_TOOLCHAIN STREQUAL "LLVM22MSVC142")
    if(NOT COMMAND soui_llvm22_verify_project OR NOT CMAKE_MT)
      message(FATAL_ERROR "The explicit LLVM manifest tool was not verified")
    endif()
    soui_llvm22_verify_project()
    set(SOUI_MT_EXECUTABLE "${CMAKE_MT}")
  else()
    get_filename_component(windows_kits_root "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
    find_program(SOUI_MT_EXECUTABLE mt.exe
      PATHS "${windows_kits_root}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64" REQUIRED)
  endif()
  add_executable(${target} WIN32 ${APP_SOURCES} "${APP_ROOT}/resources/demo.rc")
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE /utf-8 /W4 /U_DEBUG /EHsc)
  target_compile_definitions(${target} PRIVATE NDEBUG _CRT_SECURE_NO_WARNINGS ENABLE_WKE=0 SOUI_SOURCE_KERNEL_HOST SOUI_SOURCE_OVERRIDE)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  target_include_directories(${target} PRIVATE
    "${dep}/include/soui/config" "${dep}/include/soui/SOUI/include"
    "${dep}/include/soui/utilities/include" "${dep}/include/soui/components"
    "${dep}/include/json" "${dep}/include/miniblink" "${APP_ROOT}/generated"
    "${APP_ROOT}/resources")
  target_link_directories(${target} PRIVATE "${dep}/prebuilt/${arch}/soui/lib")
  target_link_libraries(${target} PRIVATE Soui::Sdk soui_zlib soui utilities
    comdlg32 mfplat mfreadwrite mfuuid propsys shlwapi windowscodecs psapi)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/${target}")
  target_link_options(${target} PRIVATE /SUBSYSTEM:WINDOWS,6.01)
  # Deployment also runs when only Web assets changed and no native relink is needed.
  add_custom_target(${target}_runtime ALL
    COMMAND "${SOUI_NODE_EXECUTABLE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/runtime-deployment.cjs" deploy
      --sdk-root "${SOUI_DISTRIBUTION_ROOT}" --application "$<TARGET_FILE_DIR:${target}>"
      --architecture "${arch}" --backend miniblink --project-root "${APP_ROOT}" --adopt-legacy
    COMMAND "${SOUI_NODE_EXECUTABLE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/deploy-resources.cjs"
      "${APP_WEB_ROOT}" "$<TARGET_FILE_DIR:${target}>" "${SOUI_DISTRIBUTION_ROOT}" "${APP_ROOT}"
    COMMAND "${SOUI_MT_EXECUTABLE}" -nologo -manifest "${APP_ROOT}/resources/app.manifest" "-outputresource:$<TARGET_FILE:${target}>;#1"
    DEPENDS ${target}
    VERBATIM)
endfunction()

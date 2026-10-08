include_guard(GLOBAL)
function(soui_add_application target)
  cmake_parse_arguments(APP "" "ROOT;WEB_ROOT" "SOURCES" ${ARGN})
  if(NOT APP_ROOT)
    set(APP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  if(NOT APP_WEB_ROOT)
    set(APP_WEB_ROOT "${APP_ROOT}/web/dist")
  endif()
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(arch x64)
  else()
    set(arch x86)
  endif()
  set(dep "${SOUI_DISTRIBUTION_ROOT}/third_party")
  get_filename_component(windows_kits_root "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
  find_program(SOUI_MT_EXECUTABLE mt.exe
    PATHS "${windows_kits_root}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64" REQUIRED)
  find_program(SOUI_NODE_EXECUTABLE node.exe REQUIRED)
  add_executable(${target} WIN32 ${APP_SOURCES} "${APP_ROOT}/resources/demo.rc")
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE /utf-8 /W4 /U_DEBUG)
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
  file(GLOB runtime_dlls "${dep}/prebuilt/${arch}/soui/bin/*.dll")
  list(FILTER runtime_dlls EXCLUDE REGEX "(souid|utilitiesd|render-gdid|imgdecoder-gdipd|soui-sys-resourced)\\.dll$")
  file(GLOB_RECURSE crt_dlls "${dep}/prebuilt/${arch}/runtime/*.dll")
  # Deployment also runs when only Web assets changed and no native relink is needed.
  add_custom_target(${target}_runtime ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${runtime_dlls} ${crt_dlls} "$<TARGET_FILE_DIR:${target}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${dep}/prebuilt/${arch}/miniblink/node.dll" "$<TARGET_FILE_DIR:${target}>/node.dll"
    COMMAND "${SOUI_NODE_EXECUTABLE}" "${SOUI_DISTRIBUTION_ROOT}/tools/internal/deploy-resources.cjs"
      "${APP_WEB_ROOT}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND "${SOUI_MT_EXECUTABLE}" -nologo -manifest "${APP_ROOT}/resources/app.manifest" "-outputresource:$<TARGET_FILE:${target}>;#1"
    DEPENDS ${target}
    VERBATIM)
endfunction()

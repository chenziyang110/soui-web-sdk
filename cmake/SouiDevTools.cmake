include_guard(GLOBAL)

# Package deployment is distinct from application permission. Enabling this
# build option never grants a session or changes the application's default.
option(SOUI_DEVTOOLS_PACKAGE_ENABLED "Deploy an explicitly approved offline MiniBlink DevTools package" OFF)
set(SOUI_DEVTOOLS_FRONTEND_ROOT "" CACHE PATH "Root containing the approved frontend, paired kernel and legal materials")
set(SOUI_DEVTOOLS_KERNEL_RELATIVE "" CACHE STRING "Approved kernel path relative to the package root")
set(SOUI_DEVTOOLS_KERNEL_SHA256 "" CACHE STRING "Externally approved SHA-256 of the paired kernel image")
set(SOUI_DEVTOOLS_MANIFEST_SHA256 "" CACHE STRING "Externally approved SHA-256 of approved-manifest.json")
set(SOUI_DEVTOOLS_APPROVAL_RELATIVE "" CACHE STRING "External approval document path relative to the package root")
set(SOUI_DEVTOOLS_APPROVAL_SHA256 "" CACHE STRING "Externally approved SHA-256 of the package approval document")

# The application owns VERSION after generation. Package metadata identifies the
# selected SDK, whose public header retains the complete prerelease version.
function(_soui_devtools_sdk_version distribution_root output_variable)
  set(version_header "${distribution_root}/sdk/include/soui/version.h")
  set(sdk_lock "${distribution_root}/sdk-lock.json")
  if(NOT EXISTS "${version_header}")
    message(FATAL_ERROR "The selected SDK does not contain sdk/include/soui/version.h")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${version_header}")
  file(STRINGS "${version_header}" version_declarations
    REGEX "^[ \t]*#[ \t]*define[ \t]+SOUI_SDK_VERSION_STRING([ \t]|$)")
  list(LENGTH version_declarations declaration_count)
  if(NOT declaration_count EQUAL 1)
    message(FATAL_ERROR "The selected SDK must declare SOUI_SDK_VERSION_STRING exactly once")
  endif()
  list(GET version_declarations 0 version_declaration)
  if(NOT version_declaration MATCHES "^[ \t]*#[ \t]*define[ \t]+SOUI_SDK_VERSION_STRING[ \t]+\"([^\"]+)\"[ \t]*(//.*)?$")
    message(FATAL_ERROR "The selected SDK has an invalid SOUI_SDK_VERSION_STRING declaration")
  endif()
  set(sdk_version "${CMAKE_MATCH_1}")
  if(NOT sdk_version MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-(preview|rc)\\.(0|[1-9][0-9]*))?$")
    message(FATAL_ERROR "The selected SDK has an invalid complete SDK version")
  endif()
  if(EXISTS "${sdk_lock}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sdk_lock}")
    file(READ "${sdk_lock}" lock_json)
    string(JSON lock_schema ERROR_VARIABLE lock_error GET "${lock_json}" schema)
    if(lock_error OR NOT lock_schema STREQUAL "soui.sdk-lock/v1")
      message(FATAL_ERROR "The selected SDK has an invalid sdk-lock.json schema")
    endif()
    string(JSON locked_sdk_version ERROR_VARIABLE lock_error GET "${lock_json}" manifest version)
    if(lock_error OR NOT "${locked_sdk_version}" STREQUAL "${sdk_version}")
      message(FATAL_ERROR "The selected SDK header version does not match sdk-lock.json manifest.version")
    endif()
  endif()
  set(${output_variable} "${sdk_version}" PARENT_SCOPE)
endfunction()

# Call after the normal runtime target exists. The application runtime command
# must copy $<TARGET_PROPERTY:target,SOUI_DEVTOOLS_KERNEL_DEPLOY_SOURCE> to
# node.dll, with the normal prebuilt source property assigned before this call.
function(soui_deploy_devtools target)
  cmake_parse_arguments(DT "" "RUNTIME_TARGET" "" ${ARGN})
  if(DT_UNPARSED_ARGUMENTS OR NOT TARGET ${target})
    message(FATAL_ERROR "soui_deploy_devtools requires an existing application target")
  endif()
  if(NOT SOUI_DEVTOOLS_PACKAGE_ENABLED)
    return()
  endif()
  if(NOT DT_RUNTIME_TARGET OR NOT TARGET ${DT_RUNTIME_TARGET})
    message(FATAL_ERROR "Enabled DevTools packaging requires RUNTIME_TARGET naming the application's normal runtime deployment")
  endif()
  foreach(required SOUI_DEVTOOLS_FRONTEND_ROOT SOUI_DEVTOOLS_KERNEL_RELATIVE SOUI_DEVTOOLS_KERNEL_SHA256
      SOUI_DEVTOOLS_MANIFEST_SHA256 SOUI_DEVTOOLS_APPROVAL_RELATIVE SOUI_DEVTOOLS_APPROVAL_SHA256)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
      message(FATAL_ERROR "Enabled DevTools packaging requires explicit ${required}; no package identity is inferred")
    endif()
  endforeach()
  if(NOT IS_ABSOLUTE "${SOUI_DEVTOOLS_FRONTEND_ROOT}" OR NOT IS_DIRECTORY "${SOUI_DEVTOOLS_FRONTEND_ROOT}")
    message(FATAL_ERROR "SOUI_DEVTOOLS_FRONTEND_ROOT must be an existing local absolute package root")
  endif()
  foreach(digest SOUI_DEVTOOLS_KERNEL_SHA256 SOUI_DEVTOOLS_MANIFEST_SHA256 SOUI_DEVTOOLS_APPROVAL_SHA256)
    string(LENGTH "${${digest}}" length)
    if(NOT length EQUAL 64 OR NOT "${${digest}}" MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "${digest} must be an explicit lowercase SHA-256 approval pin")
    endif()
  endforeach()
  foreach(relative SOUI_DEVTOOLS_KERNEL_RELATIVE SOUI_DEVTOOLS_APPROVAL_RELATIVE)
    if(IS_ABSOLUTE "${${relative}}" OR "${${relative}}" MATCHES "[;\\\\]" OR "${${relative}}" MATCHES "(^|/)\\.\\.?(/|$)")
      message(FATAL_ERROR "${relative} must be a plain relative package path using forward slashes")
    endif()
  endforeach()
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(architecture x64)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(architecture x86)
  else()
    message(FATAL_ERROR "Offline MiniBlink DevTools packaging supports x86 and x64 applications only")
  endif()
  if(NOT SOUI_NODE_EXECUTABLE)
    find_program(SOUI_NODE_EXECUTABLE NAMES node.exe node REQUIRED)
  endif()
  if(NOT EXISTS "${SOUI_DISTRIBUTION_ROOT}/tools/internal/devtools-package.cjs")
    message(FATAL_ERROR "The selected SDK does not contain the offline DevTools package tool")
  endif()
  _soui_devtools_sdk_version("${SOUI_DISTRIBUTION_ROOT}" sdk_version)
  set(prepared "${CMAKE_CURRENT_BINARY_DIR}/soui-devtools/${target}")
  set(tool "${SOUI_DISTRIBUTION_ROOT}/tools/internal/devtools-package.cjs")
  add_custom_target(${target}_devtools_prepare
    COMMAND "${SOUI_NODE_EXECUTABLE}" "${tool}" prepare
      --frontend-root "${SOUI_DEVTOOLS_FRONTEND_ROOT}"
      --kernel-relative "${SOUI_DEVTOOLS_KERNEL_RELATIVE}"
      --kernel-sha256 "${SOUI_DEVTOOLS_KERNEL_SHA256}"
      --manifest-sha256 "${SOUI_DEVTOOLS_MANIFEST_SHA256}"
      --approval-relative "${SOUI_DEVTOOLS_APPROVAL_RELATIVE}"
      --approval-sha256 "${SOUI_DEVTOOLS_APPROVAL_SHA256}"
      --architecture "${architecture}" --sdk-version "${sdk_version}" --output "${prepared}"
    BYPRODUCTS "${prepared}/soui_devtools_package.h" "${prepared}/package/kernel/node.dll"
    VERBATIM)
  add_dependencies(${target} ${target}_devtools_prepare)
  target_include_directories(${target} PRIVATE "${prepared}")
  target_compile_definitions(${target} PRIVATE SOUI_DEVTOOLS_PACKAGE_AVAILABLE=1)
  set_property(TARGET ${target} PROPERTY SOUI_DEVTOOLS_KERNEL_DEPLOY_SOURCE "${prepared}/package/kernel/node.dll")
  add_custom_target(${target}_devtools_runtime ALL
    COMMAND "${SOUI_NODE_EXECUTABLE}" "${tool}" deploy
      --prepared-root "${prepared}" --application-output "$<TARGET_FILE_DIR:${target}>"
      --kernel-sha256 "${SOUI_DEVTOOLS_KERNEL_SHA256}"
      --manifest-sha256 "${SOUI_DEVTOOLS_MANIFEST_SHA256}"
      --approval-sha256 "${SOUI_DEVTOOLS_APPROVAL_SHA256}"
      --architecture "${architecture}" --sdk-version "${sdk_version}"
    DEPENDS ${DT_RUNTIME_TARGET}
    VERBATIM)
endfunction()

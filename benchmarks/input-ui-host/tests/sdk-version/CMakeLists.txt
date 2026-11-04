cmake_minimum_required(VERSION 3.24)
project(InputUiSdkVersionContracts LANGUAGES NONE)
include(CMakePackageConfigHelpers)
set(_helper "${CMAKE_CURRENT_LIST_DIR}/../../sdk_version.cmake")
set(_passed 0)

# Each child configures the real resolver against a tiny imported SDK target.
# No compiler, SDK implementation, Host, window or duplicate version parser.
function(version_case name)
  cmake_parse_arguments(CASE "SOURCE_TARGET" "SOURCE_VERSION;REQUEST;HEADER;PACKAGE;EXPECT_RAW;EXPECT_PACKAGE;ERROR" "" ${ARGN})
  if(NOT CASE_SOURCE_VERSION)
    set(CASE_SOURCE_VERSION "0.7.0-preview.1")
  endif()
  if(NOT CASE_HEADER)
    set(CASE_HEADER "0.7.0-preview.1")
  endif()
  if(NOT CASE_PACKAGE)
    set(CASE_PACKAGE "0.7.0")
  endif()
  set(_case "${CMAKE_CURRENT_BINARY_DIR}/cases/${name}")
  set(_repo "${_case}/repo")
  set(_install "${_case}/install")
  file(MAKE_DIRECTORY "${_repo}/benchmarks/input-ui-host" "${_repo}/sdk/include/soui"
    "${_install}/include/soui" "${_install}/lib/cmake/SouiSdk")
  configure_file("${_helper}" "${_repo}/benchmarks/input-ui-host/sdk_version.cmake" COPYONLY)
  file(WRITE "${_repo}/VERSION" "${CASE_SOURCE_VERSION}\n")
  file(WRITE "${_repo}/sdk/include/soui/version.h" "#define SOUI_SDK_VERSION_STRING \"${CASE_SOURCE_VERSION}\"\n")
  file(WRITE "${_install}/include/soui/version.h" "#define SOUI_SDK_VERSION_STRING \"${CASE_HEADER}\"\n")
  file(WRITE "${_install}/lib/cmake/SouiSdk/SouiSdkConfig.cmake"
    "add_library(Soui::Sdk INTERFACE IMPORTED)\nset_target_properties(Soui::Sdk PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"${_install}/include\")\n")
  write_basic_package_version_file("${_install}/lib/cmake/SouiSdk/SouiSdkConfigVersion.cmake"
    VERSION "${CASE_PACKAGE}" COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
  set(_body "cmake_minimum_required(VERSION 3.24)\nproject(IdentityCase LANGUAGES NONE)\n")
  if(CASE_SOURCE_TARGET)
    string(APPEND _body "add_library(source_sdk INTERFACE)\nadd_library(Soui::Sdk ALIAS source_sdk)\n")
  endif()
  string(APPEND _body
    "set(SOUI_INPUT_UI_SDK_VERSION \"${CASE_REQUEST}\" CACHE STRING \"fixture\")\n"
    "set(SouiSdk_DIR \"${_install}/lib/cmake/SouiSdk\")\n"
    "include(\"${_repo}/benchmarks/input-ui-host/sdk_version.cmake\")\nsoui_input_ui_resolve_sdk()\n")
  if(NOT CASE_ERROR)
    string(APPEND _body
      "if(NOT SOUI_INPUT_UI_EFFECTIVE_SDK_VERSION STREQUAL \"${CASE_EXPECT_RAW}\" OR NOT SOUI_INPUT_UI_PACKAGE_VERSION STREQUAL \"${CASE_EXPECT_PACKAGE}\")\n"
      "message(FATAL_ERROR \"Wrong effective raw/numeric identity\")\nendif()\n"
      "if(NOT SOUI_INPUT_UI_SDK_VERSION STREQUAL \"${CASE_REQUEST}\")\nmessage(FATAL_ERROR \"Resolver rewrote explicit/default cache\")\nendif()\n")
    if(CASE_SOURCE_TARGET)
      string(APPEND _body "if(NOT SOUI_INPUT_UI_SDK_IDENTITY_KIND STREQUAL \"source\")\nmessage(FATAL_ERROR \"Source target was replaced\")\nendif()\n")
    else()
      string(APPEND _body "if(NOT SOUI_INPUT_UI_SDK_IDENTITY_KIND STREQUAL \"installed\")\nmessage(FATAL_ERROR \"Installed target identity missing\")\nendif()\n")
    endif()
  endif()
  file(WRITE "${_case}/CMakeLists.txt" "${_body}")
  set(_generator_options -G "${CMAKE_GENERATOR}")
  if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND _generator_options -A "${CMAKE_GENERATOR_PLATFORM}")
  endif()
  if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND _generator_options -T "${CMAKE_GENERATOR_TOOLSET}")
  endif()
  if(CMAKE_SYSTEM_VERSION)
    list(APPEND _generator_options "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_case}" -B "${_case}/build" ${_generator_options}
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 30)
  file(WRITE "${_case}/configure.log" "${_stdout}${_stderr}\nresult=${_result}\n")
  if(CASE_ERROR)
    if(_result STREQUAL "0" OR NOT "${_stdout}${_stderr}" MATCHES "${CASE_ERROR}")
      message(FATAL_ERROR "${name}: expected rejection '${CASE_ERROR}', got '${_result}':\n${_stdout}${_stderr}")
    endif()
  elseif(NOT _result STREQUAL "0")
    message(FATAL_ERROR "${name}: expected successful identity resolution:\n${_stdout}${_stderr}")
  endif()
  math(EXPR _next "${_passed}+1")
  set(_passed "${_next}" PARENT_SCOPE)
  message(STATUS "Identity contract passed: ${name}")
endfunction()

version_case(default-preview EXPECT_RAW 0.7.0-preview.1 EXPECT_PACKAGE 0.7.0)
version_case(explicit-baseline REQUEST 0.6.0 HEADER 0.6.0 PACKAGE 0.6.0 EXPECT_RAW 0.6.0 EXPECT_PACKAGE 0.6.0)
version_case(explicit-rc REQUEST 0.7.0-rc.0 HEADER 0.7.0-rc.0 EXPECT_RAW 0.7.0-rc.0 EXPECT_PACKAGE 0.7.0)
version_case(max-safe-prerelease REQUEST 0.7.0-rc.9007199254740991 HEADER 0.7.0-rc.9007199254740991
  EXPECT_RAW 0.7.0-rc.9007199254740991 EXPECT_PACKAGE 0.7.0)
version_case(same-tuple-other-preview REQUEST 0.7.0-preview.1 HEADER 0.7.0-preview.2 ERROR "SDK raw release mismatch")
version_case(stable-is-not-preview REQUEST 0.7.0 HEADER 0.7.0-preview.1 ERROR "SDK raw release mismatch")
version_case(exact-package-required REQUEST 0.7.0-preview.1 PACKAGE 0.8.0 ERROR "exactly[ \t\r\n]+matches requested version")
version_case(source-target-stale-cache SOURCE_TARGET REQUEST 0.6.0 HEADER 0.6.0 PACKAGE 0.6.0
  EXPECT_RAW 0.7.0-preview.1 EXPECT_PACKAGE 0.7.0)
version_case(source-target-irrelevant-cache SOURCE_TARGET REQUEST old-excluded-benchmark
  EXPECT_RAW 0.7.0-preview.1 EXPECT_PACKAGE 0.7.0)
version_case(leading-zero REQUEST 0.07.0 ERROR "Invalid SDK release identity")
version_case(other-channel REQUEST 0.7.0-beta.1 ERROR "Invalid SDK release identity")
version_case(build-metadata REQUEST 0.7.0-preview.1+local ERROR "Invalid SDK release identity")
version_case(unsafe-integer REQUEST 0.7.0-preview.9007199254740992 ERROR "safe-integer limit")
version_case(raw-length-bound REQUEST 9007199254740991.9007199254740991.9007199254740991-preview.9007199254740991
  ERROR "Invalid SDK release identity")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/contract-result.json"
  "{\"schema\":\"soui.input-ui-sdk-version-contracts/v1\",\"passed\":${_passed},\"configuredOnly\":true,\"guiExecuted\":false}\n")

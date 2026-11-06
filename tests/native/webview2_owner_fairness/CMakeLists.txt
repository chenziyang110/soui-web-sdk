cmake_minimum_required(VERSION 3.24)
project(SouiNativeOwnerFairness LANGUAGES CXX)
if(NOT WIN32 OR NOT (MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
  message(FATAL_ERROR "Use the frozen Windows MSVC-front-end toolchain")
endif()
set(MATCHED_SDK_ROOT "" CACHE PATH "Exact independently installed SDK prefix")
set(MATCHED_SDK_VERSION "" CACHE STRING "Mandatory exact raw installed SDK release")
set(SOUI_FAIRNESS_BUILD_ID "" CACHE STRING "Mandatory unique consumer build identity")
set(SOUI_FAIRNESS_OWNER_LOOP "" CACHE STRING "Mandatory measurement mode: legacy or sdk")
set_property(CACHE SOUI_FAIRNESS_OWNER_LOOP PROPERTY STRINGS legacy sdk)
if(NOT SOUI_FAIRNESS_OWNER_LOOP MATCHES "^(legacy|sdk)$")
  message(FATAL_ERROR "Select SOUI_FAIRNESS_OWNER_LOOP=legacy or sdk explicitly")
endif()
set(_sdk_owner_loop 0)
if(SOUI_FAIRNESS_OWNER_LOOP STREQUAL "sdk")
  set(_sdk_owner_loop 1)
endif()
if(NOT IS_ABSOLUTE "${MATCHED_SDK_ROOT}" OR "${MATCHED_SDK_VERSION}" STREQUAL "" OR
   NOT SOUI_FAIRNESS_BUILD_ID MATCHES "^[A-Za-z0-9_.-]+$")
  message(FATAL_ERROR "Set MATCHED_SDK_ROOT, MATCHED_SDK_VERSION and a unique SOUI_FAIRNESS_BUILD_ID explicitly")
endif()
string(LENGTH "${SOUI_FAIRNESS_BUILD_ID}" _id_length)
if(_id_length GREATER 80)
  message(FATAL_ERROR "Build identity exceeds bound")
endif()
set(SOUI_FAIRNESS_VERSION_HELPER
  "${CMAKE_CURRENT_LIST_DIR}/../../../benchmarks/input-ui-host/sdk_version.cmake"
  CACHE FILEPATH "Existing SDK raw/numeric release identity helper")
include("${SOUI_FAIRNESS_VERSION_HELPER}")
soui_input_ui_package_version("${MATCHED_SDK_VERSION}" _package_version)
find_package(SouiSdk ${_package_version} EXACT CONFIG REQUIRED COMPONENTS WebView2
  PATHS "${MATCHED_SDK_ROOT}/lib/cmake/SouiSdk" NO_DEFAULT_PATH)
file(REAL_PATH "${MATCHED_SDK_ROOT}" _requested)
file(REAL_PATH "${SouiSdk_DIR}/../../.." _actual)
if(NOT _actual STREQUAL _requested)
  message(FATAL_ERROR "Resolved SDK prefix differs")
endif()
get_target_property(_imported Soui::Sdk IMPORTED)
if(NOT _imported)
  message(FATAL_ERROR "This diagnostic requires an independently installed SDK")
endif()
set(SOUI_INPUT_UI_SDK_VERSION "${MATCHED_SDK_VERSION}")
soui_input_ui_resolve_sdk()
if(_sdk_owner_loop)
  get_filename_component(_sdk_header_directory "${SOUI_INPUT_UI_SDK_VERSION_HEADER}" DIRECTORY)
  if(NOT EXISTS "${_sdk_header_directory}/host/windows_owner_loop.h")
    message(FATAL_ERROR "SDK mode requires the actual installed public windows_owner_loop.h")
  endif()
endif()
file(SHA256 "${SOUI_INPUT_UI_SDK_VERSION_HEADER}" _version_header_sha256)

# A normal SDK build receipt is external evidence, not an installed public API.
# Preserve its exact identity when supplied; do not invent or self-certify its
# BuildID without the runner's archive/link/receipt closure verification.
set(SOUI_FAIRNESS_SDK_BUILD_RECEIPT "" CACHE FILEPATH "Optional normal SDK build receipt")
set(_sdk_receipt_sha256 "unknown")
set(_sdk_receipt_bytes "unknown")
if(NOT "${SOUI_FAIRNESS_SDK_BUILD_RECEIPT}" STREQUAL "")
  if(NOT IS_ABSOLUTE "${SOUI_FAIRNESS_SDK_BUILD_RECEIPT}" OR
     NOT EXISTS "${SOUI_FAIRNESS_SDK_BUILD_RECEIPT}" OR
     IS_DIRECTORY "${SOUI_FAIRNESS_SDK_BUILD_RECEIPT}")
    message(FATAL_ERROR "SDK build receipt must be an explicit actual file")
  endif()
  file(SIZE "${SOUI_FAIRNESS_SDK_BUILD_RECEIPT}" _sdk_receipt_bytes)
  if(_sdk_receipt_bytes GREATER 16777216)
    message(FATAL_ERROR "SDK build receipt exceeds the cold metadata bound")
  endif()
  file(SHA256 "${SOUI_FAIRNESS_SDK_BUILD_RECEIPT}" _sdk_receipt_sha256)
endif()

add_executable(SouiWebView2OwnerFairness main.cpp paint_probe.cpp)
target_compile_features(SouiWebView2OwnerFairness PRIVATE cxx_std_17)
target_compile_options(SouiWebView2OwnerFairness PRIVATE /W4 /EHsc /utf-8 /U_DEBUG)
target_compile_definitions(SouiWebView2OwnerFairness PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601
  SOUI_FAIRNESS_BUILD_ID="${SOUI_FAIRNESS_BUILD_ID}"
  SOUI_FAIRNESS_USE_SDK_OWNER_LOOP=${_sdk_owner_loop}
  SOUI_FAIRNESS_OWNER_LOOP_MODE="${SOUI_FAIRNESS_OWNER_LOOP}"
  SOUI_FAIRNESS_RAW_VERSION="${SOUI_INPUT_UI_EFFECTIVE_SDK_VERSION}"
  SOUI_FAIRNESS_PACKAGE_VERSION="${SOUI_INPUT_UI_PACKAGE_VERSION}")
set_property(TARGET SouiWebView2OwnerFairness PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_libraries(SouiWebView2OwnerFairness PRIVATE Soui::WebView2 Soui::ActionJson user32 gdi32 ole32)
target_link_options(SouiWebView2OwnerFairness PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/r41-owner-Release.map")
add_executable(R41NativeStripInput input.cpp)
target_link_libraries(R41NativeStripInput PRIVATE Soui::ActionJson user32 gdi32)
target_compile_features(R41NativeStripInput PRIVATE cxx_std_17)
target_compile_options(R41NativeStripInput PRIVATE /W4 /EHsc /utf-8 /U_DEBUG)
target_compile_definitions(R41NativeStripInput PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
set_property(TARGET R41NativeStripInput PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
add_executable(R41PolicyContracts tests/policy-contracts.cpp)
target_compile_features(R41PolicyContracts PRIVATE cxx_std_17)
target_compile_options(R41PolicyContracts PRIVATE /W4 /EHsc /utf-8 /U_DEBUG)
set_property(TARGET R41PolicyContracts PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sdk-identity-$<CONFIG>.txt"
  CONTENT "consumer_build_id=${SOUI_FAIRNESS_BUILD_ID}\nowner_loop=${SOUI_FAIRNESS_OWNER_LOOP}\nretrieval_observer=soui.owner-getmessage-snapshot/v1\nraw=${SOUI_INPUT_UI_EFFECTIVE_SDK_VERSION}\npackage=${SOUI_INPUT_UI_PACKAGE_VERSION}\nprefix=${_requested}\nheader=${SOUI_INPUT_UI_SDK_VERSION_HEADER}\nheader_sha256=${_version_header_sha256}\nsdk=$<TARGET_FILE:Soui::Sdk>\nwebview2=$<TARGET_FILE:Soui::WebView2>\nloader=$<TARGET_FILE:Soui::WebView2Loader>\nsdk_build_receipt=${SOUI_FAIRNESS_SDK_BUILD_RECEIPT}\nsdk_build_receipt_bytes=${_sdk_receipt_bytes}\nsdk_build_receipt_sha256=${_sdk_receipt_sha256}\nsdk_build_id=unknown\nsdk_build_provenance_verified=false\n")
add_custom_command(TARGET SouiWebView2OwnerFairness POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:SouiWebView2OwnerFairness>/resources"
  VERBATIM)

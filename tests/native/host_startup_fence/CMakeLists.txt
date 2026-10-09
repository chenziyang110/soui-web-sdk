cmake_minimum_required(VERSION 3.24)
set(STARTUP_TEST_DIR "${CMAKE_CURRENT_LIST_DIR}")
if(NOT SOUI_DISTRIBUTION_ROOT)
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${STARTUP_TEST_DIR}/../../.." ABSOLUTE)
endif()
include("${STARTUP_TEST_DIR}/verify-inputs.cmake")
if(VERIFY_ONLY)
  return()
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiPublicHostStartupFence LANGUAGES C CXX)
  set(SOUI_BUILD_WEBVIEW2 OFF CACHE BOOL "" FORCE)
  set(SOUI_BUILD_UIA OFF CACHE BOOL "" FORCE)
  add_subdirectory("${SOUI_DISTRIBUTION_ROOT}/sdk" sdk)
  enable_testing()
endif()
if(NOT WIN32 OR NOT MSVC OR NOT TARGET soui_sdk OR NOT TARGET Soui::Sdk OR NOT TARGET Soui::ActionJson)
  message(FATAL_ERROR "Actual Host startup regressions require the normal Windows/MSVC SDK")
endif()
if(NOT IS_ABSOLUTE "${SOUI_HOST_STARTUP_KERNEL}" OR NOT EXISTS "${SOUI_HOST_STARTUP_KERNEL}")
  message(FATAL_ERROR "Explicit actual kernel path is required; there is no no-kernel success mode")
endif()
string(LENGTH "${SOUI_HOST_STARTUP_KERNEL_SHA256}" startup_kernel_sha_length)
if(NOT startup_kernel_sha_length EQUAL 64 OR NOT SOUI_HOST_STARTUP_KERNEL_SHA256 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Explicit lowercase actual kernel SHA256 is required")
endif()
file(SHA256 "${SOUI_HOST_STARTUP_KERNEL}" startup_actual_kernel_sha)
if(NOT startup_actual_kernel_sha STREQUAL SOUI_HOST_STARTUP_KERNEL_SHA256)
  message(FATAL_ERROR "Actual startup kernel bytes changed")
endif()
# The normal SDK target keeps its production Host source and has no observer definition.
get_target_property(startup_normal_sources soui_sdk SOURCES)
list(LENGTH startup_normal_sources startup_normal_source_count)
list(FIND startup_normal_sources "${SOUI_DISTRIBUTION_ROOT}/sdk/src/host/soui_miniblink_host.cpp" startup_normal_host_index)
get_target_property(startup_normal_definitions soui_sdk COMPILE_DEFINITIONS)
if(NOT startup_normal_source_count EQUAL 25 OR startup_normal_host_index EQUAL -1
   OR startup_normal_definitions MATCHES "SOUI_STARTUP_FENCE_TEST_OBSERVER")
  message(FATAL_ERROR "The actual normal SDK target must retain its production Host without the observer")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(startup_generated_host "${CMAKE_CURRENT_BINARY_DIR}/generated/startup_observed_host.cpp")
# Configure one placeholder so inserted C++/JS bytes are not configured again.
# The writer adds one final LF; remove only that LF from the substituted value.
if(NOT startup_generated_host_body MATCHES "\n$")
  message(FATAL_ERROR "Reviewed generated Host must end in LF")
endif()
string(REGEX REPLACE "\n$" "" startup_generated_host_config_body "${startup_generated_host_body}")
file(CONFIGURE OUTPUT "${startup_generated_host}" CONTENT "@startup_generated_host_config_body@"
  @ONLY NEWLINE_STYLE UNIX)
file(SHA256 "${startup_generated_host}" startup_generated_sha)
if(NOT startup_generated_sha STREQUAL startup_expected_generated_sha)
  message(FATAL_ERROR "Generated private Host bytes differ from the reviewed six insertions")
endif()
add_executable(soui_public_host_startup_fence WIN32 fixture.cpp "${startup_generated_host}")
target_compile_features(soui_public_host_startup_fence PRIVATE cxx_std_17)
target_link_libraries(soui_public_host_startup_fence PRIVATE Soui::Sdk Soui::ActionJson)
target_include_directories(soui_public_host_startup_fence PRIVATE
  "${STARTUP_TEST_DIR}" "${SOUI_DISTRIBUTION_ROOT}/sdk/src/host")
target_compile_definitions(soui_public_host_startup_fence PRIVATE
  ENABLE_WKE=0 SOUI_SOURCE_KERNEL_HOST NDEBUG _CRT_SECURE_NO_WARNINGS SOUI_STARTUP_FENCE_TEST_OBSERVER)
target_compile_options(soui_public_host_startup_fence PRIVATE /utf-8 /W4 /EHsc /U_DEBUG)
set_property(TARGET soui_public_host_startup_fence PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(startup_map "${CMAKE_CURRENT_BINARY_DIR}/soui_public_host_startup_fence.map")
target_link_options(soui_public_host_startup_fence PRIVATE "/MAP:${startup_map}")
add_custom_target(verify_public_host_startup_sources
  COMMAND "${CMAKE_COMMAND}" "-DSOUI_DISTRIBUTION_ROOT=${SOUI_DISTRIBUTION_ROOT}"
    "-DSTARTUP_TEST_DIR=${STARTUP_TEST_DIR}" "-DSTARTUP_GENERATED=${startup_generated_host}"
    -P "${STARTUP_TEST_DIR}/verify-inputs.cmake" VERBATIM)
add_dependencies(soui_public_host_startup_fence verify_public_host_startup_sources)
if(TARGET soui_sdk)
  add_dependencies(soui_sdk verify_public_host_startup_sources)
endif()
# MSVC MAP contributions must name the private object and never the normal archive Host member.
add_custom_command(TARGET soui_public_host_startup_fence POST_BUILD
  COMMAND "${CMAKE_COMMAND}" "-DSOUI_DISTRIBUTION_ROOT=${SOUI_DISTRIBUTION_ROOT}"
    "-DSTARTUP_TEST_DIR=${STARTUP_TEST_DIR}" "-DSTARTUP_GENERATED=${startup_generated_host}"
    "-DSTARTUP_MAP=${startup_map}" -P "${STARTUP_TEST_DIR}/verify-link.cmake" VERBATIM)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime-resources")
configure_file("${STARTUP_TEST_DIR}/resources/index.html"
  "${CMAKE_CURRENT_BINARY_DIR}/runtime-resources/index.html" COPYONLY)
foreach(schedule IN ITEMS hidden-start-registers startup-close-sent two-host-shared-engine)
  string(REPLACE "-" "_" suffix "${schedule}")
  add_test(NAME "host_${suffix}" COMMAND soui_public_host_startup_fence
    --kernel "${SOUI_HOST_STARTUP_KERNEL}" --kernel-sha "${SOUI_HOST_STARTUP_KERNEL_SHA256}"
    --resources "${CMAKE_CURRENT_BINARY_DIR}/runtime-resources"
    --output "${CMAKE_CURRENT_BINARY_DIR}/${schedule}.json" --case "${schedule}")
  set_tests_properties("host_${suffix}" PROPERTIES TIMEOUT 35 RUN_SERIAL TRUE
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" ENVIRONMENT "SOUI_DIAGNOSTIC_SCRIPT=1")
endforeach()

cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiVendorLifetimeTests LANGUAGES CXX)
  enable_testing()
endif()
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Vendor lifetime regressions require Windows and MSVC")
endif()

if(NOT DEFINED SOUI_DISTRIBUTION_ROOT OR SOUI_DISTRIBUTION_ROOT STREQUAL "")
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
get_filename_component(lifetime_vendor_root
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink" ABSOLUTE)
set(lifetime_wke_source "${lifetime_vendor_root}/wke/wke.cpp")
set(lifetime_activation_source "${lifetime_vendor_root}/net/ActivatingObjCheck.cpp")
set(lifetime_activation_header "${lifetime_vendor_root}/net/ActivatingObjCheck.h")
foreach(input IN ITEMS "${lifetime_wke_source}" "${lifetime_activation_source}" "${lifetime_activation_header}")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "Vendor lifetime regression input is missing: ${input}")
  endif()
endforeach()

# Read and extract bytes, so source line endings are never silently normalized.
# Keep the ownership algorithm in the actual vendor source rather than the test.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${lifetime_wke_source}")
file(READ "${lifetime_wke_source}" lifetime_source_hex HEX)
string(TOLOWER "${lifetime_source_hex}" lifetime_source_hex)
string(HEX "struct InteractionEndpointImpl {" lifetime_begin_hex)
string(HEX "void WKE_CALL_TYPE retainInteractionEndpoint(" lifetime_end_hex)
foreach(marker IN ITEMS begin end)
  string(REGEX MATCHALL "${lifetime_${marker}_hex}" lifetime_matches "${lifetime_source_hex}")
  list(LENGTH lifetime_matches lifetime_match_count)
  if(NOT lifetime_match_count EQUAL 1)
    message(FATAL_ERROR "Vendor lifetime source needs exactly one ${marker} marker")
  endif()
endforeach()
string(FIND "${lifetime_source_hex}" "${lifetime_begin_hex}" lifetime_begin_offset)
string(FIND "${lifetime_source_hex}" "${lifetime_end_hex}" lifetime_end_offset)
math(EXPR lifetime_begin_alignment "${lifetime_begin_offset} % 2")
math(EXPR lifetime_end_alignment "${lifetime_end_offset} % 2")
if(NOT lifetime_begin_alignment EQUAL 0 OR NOT lifetime_end_alignment EQUAL 0
    OR lifetime_end_offset LESS_EQUAL lifetime_begin_offset)
  message(FATAL_ERROR "Vendor lifetime source marker byte boundaries are invalid")
endif()
math(EXPR lifetime_fragment_length "${lifetime_end_offset} - ${lifetime_begin_offset}")
string(SUBSTRING "${lifetime_source_hex}" ${lifetime_begin_offset} ${lifetime_fragment_length} lifetime_fragment_hex)
math(EXPR lifetime_last_byte_offset "${lifetime_fragment_length} - 2")
# Exact generation admits LF-only, NUL-free fragments with a terminal LF.
# Reject unsupported input before generation; never append or trim source bytes.
string(SUBSTRING "${lifetime_fragment_hex}" ${lifetime_last_byte_offset} 2 lifetime_terminal_hex)
if(NOT lifetime_terminal_hex STREQUAL "0a")
  message(FATAL_ERROR "Vendor lifetime source fragment requires a terminal LF; missing final LF is unsupported")
endif()
set(lifetime_fragment "")
foreach(offset RANGE 0 ${lifetime_last_byte_offset} 2)
  string(SUBSTRING "${lifetime_fragment_hex}" ${offset} 2 lifetime_byte_hex)
  math(EXPR lifetime_byte "0x${lifetime_byte_hex}")
  if(lifetime_byte EQUAL 0)
    message(FATAL_ERROR "Vendor lifetime source fragment must not contain NUL bytes")
  elseif(lifetime_byte EQUAL 13)
    message(FATAL_ERROR "Vendor lifetime source fragment requires LF-only line endings; CRLF or mixed bytes are rejected")
  endif()
  string(ASCII ${lifetime_byte} lifetime_character)
  string(APPEND lifetime_fragment "${lifetime_character}")
endforeach()
set(lifetime_generated_directory "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(lifetime_generated_source "${lifetime_generated_directory}/interaction_endpoint_source.inc")
file(MAKE_DIRECTORY "${lifetime_generated_directory}")
# Generation writes the exact data after configuration. Protect source dollar
# bytes from generator-expression parsing using a temporary encoding only.
string(REPLACE "$" "$<1:$>" lifetime_generator_content "${lifetime_fragment}")
file(GENERATE OUTPUT "${lifetime_generated_source}"
  CONTENT "${lifetime_generator_content}" NEWLINE_STYLE UNIX)
file(SHA256 "${lifetime_wke_source}" lifetime_source_sha256)
string(SHA256 lifetime_fragment_sha256 "${lifetime_fragment}")
message(STATUS "Vendor lifetime wke source SHA256: ${lifetime_source_sha256}")
message(STATUS "Vendor lifetime expected endpoint fragment SHA256: ${lifetime_fragment_sha256}")

# This generated CMake helper validates source bytes before either test target
# builds. Paths and numeric bounds are command arguments, not script templates.
set(lifetime_generated_gate "${lifetime_generated_directory}/lifetime_source_bytes_check.cmake")
file(WRITE "${lifetime_generated_gate}" [==[
cmake_minimum_required(VERSION 3.24)
foreach(required IN ITEMS LIFETIME_SOURCE LIFETIME_GENERATED LIFETIME_SOURCE_SHA
    LIFETIME_BEGIN_HEX_OFFSET LIFETIME_FRAGMENT_HEX_LENGTH)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "Vendor lifetime byte check argument is missing: ${required}")
  endif()
endforeach()
if(NOT IS_ABSOLUTE "${LIFETIME_SOURCE}" OR NOT IS_ABSOLUTE "${LIFETIME_GENERATED}"
    OR NOT LIFETIME_SOURCE_SHA MATCHES "^[a-f0-9]+$"
    OR NOT LIFETIME_BEGIN_HEX_OFFSET MATCHES "^[0-9]+$"
    OR NOT LIFETIME_FRAGMENT_HEX_LENGTH MATCHES "^[0-9]+$")
  message(FATAL_ERROR "Vendor lifetime byte check arguments are invalid")
endif()
string(LENGTH "${LIFETIME_SOURCE_SHA}" source_sha_length)
if(NOT source_sha_length EQUAL 64)
  message(FATAL_ERROR "Vendor lifetime source hash length is invalid")
endif()
file(SHA256 "${LIFETIME_SOURCE}" current_source_sha)
if(NOT current_source_sha STREQUAL LIFETIME_SOURCE_SHA)
  message(FATAL_ERROR "Vendor lifetime actual source changed after configuration")
endif()
file(READ "${LIFETIME_SOURCE}" source_hex HEX)
string(TOLOWER "${source_hex}" source_hex)
string(SUBSTRING "${source_hex}" ${LIFETIME_BEGIN_HEX_OFFSET}
  ${LIFETIME_FRAGMENT_HEX_LENGTH} expected_hex)
file(READ "${LIFETIME_GENERATED}" generated_hex HEX)
string(TOLOWER "${generated_hex}" generated_hex)
if(NOT generated_hex STREQUAL expected_hex)
  message(FATAL_ERROR "Vendor lifetime generated fragment differs from actual source bytes")
endif()
]==])
add_custom_target(soui_vendor_lifetime_source_bytes
  COMMAND "${CMAKE_COMMAND}"
    "-DLIFETIME_SOURCE=${lifetime_wke_source}"
    "-DLIFETIME_GENERATED=${lifetime_generated_source}"
    "-DLIFETIME_SOURCE_SHA=${lifetime_source_sha256}"
    "-DLIFETIME_BEGIN_HEX_OFFSET=${lifetime_begin_offset}"
    "-DLIFETIME_FRAGMENT_HEX_LENGTH=${lifetime_fragment_length}"
    -P "${lifetime_generated_gate}"
  VERBATIM)

add_executable(soui_vendor_endpoint_lifetime_tests endpoint_tests.cpp "${lifetime_generated_source}")
target_include_directories(soui_vendor_endpoint_lifetime_tests PRIVATE "${lifetime_generated_directory}")
add_executable(soui_vendor_activation_lock_tests activation_tests.cpp
  "${lifetime_activation_source}" "${lifetime_activation_header}")
target_include_directories(soui_vendor_activation_lock_tests PRIVATE "${lifetime_vendor_root}")
foreach(target IN ITEMS soui_vendor_endpoint_lifetime_tests soui_vendor_activation_lock_tests)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  add_dependencies(${target} soui_vendor_lifetime_source_bytes)
endforeach()

foreach(case IN ITEMS empty-registry second-vector-growth-fault all-allocation-forbidden
    create-allocation-failure create-registry-growth-failure create-success create-invalid)
  add_test(NAME kernel.native.lifetime.endpoint-${case} COMMAND soui_vendor_endpoint_lifetime_tests ${case})
  set_tests_properties(kernel.native.lifetime.endpoint-${case} PROPERTIES TIMEOUT 10)
endforeach()
foreach(case IN ITEMS insert-failure-lock-release snapshot-failure-lock-release)
  add_test(NAME kernel.native.lifetime.activation-${case} COMMAND soui_vendor_activation_lock_tests ${case})
  set_tests_properties(kernel.native.lifetime.activation-${case} PROPERTIES TIMEOUT 10)
endforeach()

add_subdirectory(worker_lifetime)
add_subdirectory(dragdata)
add_subdirectory(ctor_shell)
add_subdirectory(legacy_drag)

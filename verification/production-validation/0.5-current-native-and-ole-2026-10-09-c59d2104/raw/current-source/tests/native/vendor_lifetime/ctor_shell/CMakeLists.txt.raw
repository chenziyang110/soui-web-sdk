cmake_minimum_required(VERSION 3.24)
# This file is both the configure entry and the mandatory pre-build byte gate.
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT OR SOUI_DISTRIBUTION_ROOT STREQUAL "")
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
endif()
get_filename_component(public_ctor_shell_vendor_root "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink" ABSOLUTE)
set(public_ctor_shell_fixture_pins
  "constructor_tests.cpp|14410|158329c75739764909b1e363f9fa1986fda5e383fcecf6741b40b7af30ad61e2"
  ".gitattributes|125|b32a221be14133b9f872795c50f40e031220f2e26259ce366251ef849af46e29"
  "extracted/web_view.h|4408|ddb34c07330d9532550349bf52600f6859906638120005349823396ec33a1bc2"
  "extracted/web_page.h|1049|08ddcb8678821b8426b6cb1126c4c121d7debf9d4b0a186fa7607847a77c3c57"
)
set(public_ctor_shell_source_pins
  "third_party/sources/miniblink/wke/wkeWebView.cpp|44094|612312f09b14d3a5c9566b28b1793ecf5f9a5d1bb13de11f95151a5e2128739a"
  "third_party/sources/miniblink/content/browser/WebPage.cpp|14334|5ba34fba9a2f44a033d08f7843efb6ca7a508d8529aa084d9ac505e576d2a45d"
  "third_party/sources/miniblink/net/ActivatingObjCheck.cpp|3100|e2f04bfb9fb0e8ddcf9a7a8bbd9ca5598ce21dccd5f035863f8630702a64842b"
  "third_party/sources/miniblink/net/ActivatingObjCheck.h|660|6e85aca76332bc4fc2e5465501c97d76a240b9bee3021b0d24a88a8cedbe28f6"
)
# source|source-byte-offset|fixture|fixture-byte-offset|exact-byte-length
set(public_ctor_shell_ranges
  "third_party/sources/miniblink/wke/wkeWebView.cpp|1121|extracted/web_view.h|76|1157"
  "third_party/sources/miniblink/wke/wkeWebView.cpp|2279|extracted/web_view.h|1310|133"
  "third_party/sources/miniblink/wke/wkeWebView.cpp|2413|extracted/web_view.h|1520|159"
  "third_party/sources/miniblink/wke/wkeWebView.cpp|31200|extracted/web_view.h|1756|2173"
  "third_party/sources/miniblink/wke/wkeWebView.cpp|30958|extracted/web_view.h|4006|241"
  "third_party/sources/miniblink/wke/wkeWebView.cpp|33374|extracted/web_view.h|4324|84"
  "third_party/sources/miniblink/content/browser/WebPage.cpp|1179|extracted/web_page.h|85|632"
  "third_party/sources/miniblink/content/browser/WebPage.cpp|1812|extracted/web_page.h|803|246"
)
foreach(kind IN ITEMS fixture source)
  foreach(record IN LISTS public_ctor_shell_${kind}_pins)
    string(REPLACE "|" ";" fields "${record}")
    list(GET fields 0 relative)
    list(GET fields 1 expected_bytes)
    list(GET fields 2 expected_sha)
    if(kind STREQUAL "fixture")
      set(input "${CMAKE_CURRENT_LIST_DIR}/${relative}")
    else()
      set(input "${SOUI_DISTRIBUTION_ROOT}/${relative}")
    endif()
    file(SIZE "${input}" actual_bytes)
    file(SHA256 "${input}" actual_sha)
    if(NOT actual_bytes EQUAL expected_bytes OR NOT actual_sha STREQUAL expected_sha)
      message(FATAL_ERROR "ctor_shell: ${kind} bytes changed: ${relative}")
    endif()
  endforeach()
endforeach()
foreach(record IN LISTS public_ctor_shell_ranges)
  string(REPLACE "|" ";" fields "${record}")
  list(GET fields 0 source)
  list(GET fields 1 source_offset)
  list(GET fields 2 fixture)
  list(GET fields 3 fixture_offset)
  list(GET fields 4 bytes)
  math(EXPR source_hex_offset "${source_offset} * 2")
  math(EXPR fixture_hex_offset "${fixture_offset} * 2")
  math(EXPR hex_length "${bytes} * 2")
  file(READ "${SOUI_DISTRIBUTION_ROOT}/${source}" source_hex HEX)
  file(READ "${CMAKE_CURRENT_LIST_DIR}/${fixture}" fixture_hex HEX)
  string(TOLOWER "${source_hex}" source_hex)
  string(TOLOWER "${fixture_hex}" fixture_hex)
  string(SUBSTRING "${source_hex}" ${source_hex_offset} ${hex_length} source_body_hex)
  string(SUBSTRING "${fixture_hex}" ${fixture_hex_offset} ${hex_length} fixture_body_hex)
  if(NOT source_body_hex STREQUAL fixture_body_hex)
    message(FATAL_ERROR "ctor_shell: source body differs from ${fixture}: ${source}")
  endif()
endforeach()
if(SOUI_VENDOR_LIFETIME_VERIFY_ONLY)
  return()
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiVendorctor_shellTests LANGUAGES CXX)
  enable_testing()
endif()
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Vendor ctor_shell regressions require Windows and MSVC")
endif()
foreach(record IN LISTS public_ctor_shell_source_pins)
  string(REPLACE "|" ";" fields "${record}")
  list(GET fields 0 relative)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SOUI_DISTRIBUTION_ROOT}/${relative}")
endforeach()
add_custom_target(soui_vendor_ctor_shell_tests_source_bytes
  COMMAND "${CMAKE_COMMAND}" "-DSOUI_VENDOR_LIFETIME_VERIFY_ONLY=ON"
    "-DSOUI_DISTRIBUTION_ROOT=${SOUI_DISTRIBUTION_ROOT}"
    -P "${CMAKE_CURRENT_LIST_FILE}" VERBATIM)
# Compile the actual activation registry TU/header, which match the tested bytes.
add_executable(soui_vendor_ctor_shell_tests constructor_tests.cpp
  "${public_ctor_shell_vendor_root}/net/ActivatingObjCheck.cpp")
target_include_directories(soui_vendor_ctor_shell_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${public_ctor_shell_vendor_root}")
target_compile_definitions(soui_vendor_ctor_shell_tests PRIVATE ENABLE_WKE=1 WIN32_LEAN_AND_MEAN)
target_compile_options(soui_vendor_ctor_shell_tests PRIVATE /EHsc /W3)
target_compile_features(soui_vendor_ctor_shell_tests PRIVATE cxx_std_17)
add_dependencies(soui_vendor_ctor_shell_tests soui_vendor_ctor_shell_tests_source_bytes)
foreach(case IN ITEMS shell-success handler-before-publication view-success activation-failure view-handler-rollback engine-terminate live-insert-terminate)
  add_test(NAME kernel.native.lifetime.ctor_shell-${case} COMMAND soui_vendor_ctor_shell_tests "${case}")
  set_tests_properties(kernel.native.lifetime.ctor_shell-${case} PROPERTIES TIMEOUT 10)
endforeach()

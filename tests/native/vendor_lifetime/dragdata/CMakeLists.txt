cmake_minimum_required(VERSION 3.24)
# This file is both the configure entry and the mandatory pre-build byte gate.
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT OR SOUI_DISTRIBUTION_ROOT STREQUAL "")
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
endif()
get_filename_component(public_dragdata_vendor_root "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink" ABSOLUTE)
set(public_dragdata_fixture_pins
  "fixture.cpp|29660|db96926a385858abdc47d1ebf63885ed4f8c6a32cb5bfbf07bbdf427bfdc19c3"
  ".gitattributes|74|c4398f74862eaba5f7f1576ffb716b48a9aa6a1b56cda767a02e5a3b5e311f14"
)
set(public_dragdata_source_pins
  "third_party/sources/miniblink/content/browser/WebPageImpl.cpp|94066|f7e5795562105d5fbfc66a6594823530d86d971963a4d8df5f82dd8799ee1264"
  "third_party/sources/miniblink/wke/wkedefine.h|70772|17485a2c5b776f9533f88d7274a91014760652ddb8af7e4280578b5292c22939"
  "third_party/sources/miniblink/wke/wkeNetHook.cpp|23531|dff4b76adf0101610d0a9b32c0364658ece480c8c849490941846f79639c3aed"
  "third_party/sources/miniblink/third_party/WebKit/public/platform/WebVector.h|5493|4330d9616d7401189b746fc4d26582aec87b588b1a6aff523964d69b791474a7"
  "third_party/sources/miniblink/third_party/WebKit/public/platform/WebDragData.h|4979|11ac2e2b6d12c05df50b896ca11cebc46afb89b19cbf7f4f091e6f1e490365ad"
  "third_party/sources/miniblink/content/ui/DragHandle.cpp|52686|083f78bf2a5921ae2ce8fbb4b736a58dea0e8f536f7c8afb965ca4128942149e"
  "third_party/sources/miniblink/content/browser/CheckReEnter.cpp|761|fde567d3c8476902f681190b0338d8ba387e5f63f0f16bf0f9469276d8455f05"
)
# source|source-byte-offset|fixture|fixture-byte-offset|exact-byte-length
set(public_dragdata_ranges
  "third_party/sources/miniblink/wke/wkedefine.h|10599|fixture.cpp|2135|2137"
  "third_party/sources/miniblink/wke/wkeNetHook.cpp|10495|fixture.cpp|4446|511"
  "third_party/sources/miniblink/third_party/WebKit/public/platform/WebVector.h|2168|fixture.cpp|5172|3296"
  "third_party/sources/miniblink/third_party/WebKit/public/platform/WebDragData.h|1927|fixture.cpp|9535|1448"
  "third_party/sources/miniblink/third_party/WebKit/public/platform/WebDragData.h|3980|fixture.cpp|11094|74"
  "third_party/sources/miniblink/content/ui/DragHandle.cpp|9398|fixture.cpp|12009|2470"
  "third_party/sources/miniblink/content/ui/DragHandle.cpp|30464|fixture.cpp|15416|344"
  "third_party/sources/miniblink/content/browser/CheckReEnter.cpp|550|fixture.cpp|16289|199"
  "third_party/sources/miniblink/content/browser/WebPageImpl.cpp|67639|fixture.cpp|17426|173"
  "third_party/sources/miniblink/content/browser/WebPageImpl.cpp|68016|fixture.cpp|17738|5656"
)
foreach(kind IN ITEMS fixture source)
  foreach(record IN LISTS public_dragdata_${kind}_pins)
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
      message(FATAL_ERROR "dragdata: ${kind} bytes changed: ${relative}")
    endif()
  endforeach()
endforeach()
foreach(record IN LISTS public_dragdata_ranges)
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
    message(FATAL_ERROR "dragdata: source body differs from ${fixture}: ${source}")
  endif()
endforeach()
if(SOUI_VENDOR_LIFETIME_VERIFY_ONLY)
  return()
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiVendordragdataTests LANGUAGES CXX)
  enable_testing()
endif()
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Vendor dragdata regressions require Windows and MSVC")
endif()
foreach(record IN LISTS public_dragdata_source_pins)
  string(REPLACE "|" ";" fields "${record}")
  list(GET fields 0 relative)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SOUI_DISTRIBUTION_ROOT}/${relative}")
endforeach()
add_custom_target(soui_vendor_dragdata_ownership_tests_source_bytes
  COMMAND "${CMAKE_COMMAND}" "-DSOUI_VENDOR_LIFETIME_VERIFY_ONLY=ON"
    "-DSOUI_DISTRIBUTION_ROOT=${SOUI_DISTRIBUTION_ROOT}"
    -P "${CMAKE_CURRENT_LIST_FILE}" VERBATIM)
add_executable(soui_vendor_dragdata_ownership_tests fixture.cpp)
target_compile_options(soui_vendor_dragdata_ownership_tests PRIVATE /EHsc /utf-8 /W4)
target_compile_definitions(soui_vendor_dragdata_ownership_tests PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
set_property(TARGET soui_vendor_dragdata_ownership_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_features(soui_vendor_dragdata_ownership_tests PRIVATE cxx_std_17)
add_dependencies(soui_vendor_dragdata_ownership_tests soui_vendor_dragdata_ownership_tests_source_bytes)
foreach(case IN ITEMS array-allocation-failure utf8-partial-string-failure utf8-later-item-failure downstream-owned-copy-failure callback-unwind-counts callback-argument-unwind-counts controlled-257-invalid legacy-257-preserved custom-257-preserved mixed-success-releases empty-success-releases)
  add_test(NAME kernel.native.lifetime.dragdata-${case} COMMAND soui_vendor_dragdata_ownership_tests "${case}")
  set_tests_properties(kernel.native.lifetime.dragdata-${case} PROPERTIES TIMEOUT 10)
endforeach()

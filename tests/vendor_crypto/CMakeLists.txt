cmake_minimum_required(VERSION 3.24)
project(soui_vendor_crypto_tests LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This test compiles the Windows Miniblink adapter")
endif()
get_filename_component(soui_sdk_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(SOUI_MINIBLINK_SOURCE_ROOT "${soui_sdk_root}/third_party/sources/miniblink" CACHE PATH "Current Miniblink source")
set(SOUI_OPENSSL_ARCHIVE "" CACHE FILEPATH "Existing same-architecture kernel OpenSSL archive")
if(NOT EXISTS "${SOUI_OPENSSL_ARCHIVE}")
  message(FATAL_ERROR "Pass the existing same-architecture openssl.lib explicitly")
endif()

add_executable(soui_web_crypto_digestor_tests
  web_crypto_digestor_tests.cpp
  "${SOUI_MINIBLINK_SOURCE_ROOT}/content/web_impl_win/WebCryptoImpl.cpp")
target_compile_features(soui_web_crypto_digestor_tests PRIVATE cxx_std_17)
target_include_directories(soui_web_crypto_digestor_tests PRIVATE
  "${SOUI_MINIBLINK_SOURCE_ROOT}"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/v8_5_7"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/v8_5_7/include"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/GDIPlus/Includes"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/npapi"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/WebKit"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/WebKit/Source"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/cef"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/icu/source/common"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/gen/blink"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/gen/blink/core"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/gen/blink/bindings/modules/v8"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/gen/blink/bindings/core/v8"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/gpu"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/khronos"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/libxml/src/include"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/libxml/win32/include"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/core"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/config"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/codec"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/device"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/effects"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/images"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/pathops"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/ports"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/private"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/third_party/skia/include/utils"
  "${SOUI_MINIBLINK_SOURCE_ROOT}/node/openssl/openssl/include")
target_compile_definitions(soui_web_crypto_digestor_tests PRIVATE
  WIN32 NDEBUG _WINDOWS BUILDING_CEF_SHARED INSIDE_BLINK=1
  BLINK_IMPLEMENTATION=1 ENABLE_OILPAN=1 ENABLE_WKE=1)
if(MSVC)
  target_compile_options(soui_web_crypto_digestor_tests PRIVATE /FIconfig.h /Gy)
  target_link_options(soui_web_crypto_digestor_tests PRIVATE /OPT:REF /INCREMENTAL:NO)
endif()
set_property(TARGET soui_web_crypto_digestor_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
target_link_libraries(soui_web_crypto_digestor_tests PRIVATE
  "${SOUI_OPENSSL_ARCHIVE}" ws2_32 crypt32 advapi32 user32 gdi32)

enable_testing()
add_test(NAME vendor.crypto.digestor COMMAND soui_web_crypto_digestor_tests)
set_tests_properties(vendor.crypto.digestor PROPERTIES TIMEOUT 10)

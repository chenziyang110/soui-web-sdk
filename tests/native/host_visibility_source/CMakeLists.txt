# Production fragments retain their original UTF-8 bytes; explicit dependencies are faked.
find_program(NODE_EXECUTABLE node REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/browser/HostVisibilityPolicy.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/browser/WebPageImpl.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/common/InputFrameTrace.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/common/PaintLifetime.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/DrawFrameWakePolicy.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/LayerTreeHost.cpp")
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" COMMAND_ERROR_IS_FATAL ANY)
add_executable(soui_host_visibility_source_tests fixture.cpp)
target_compile_features(soui_host_visibility_source_tests PRIVATE cxx_std_17)
target_compile_options(soui_host_visibility_source_tests PRIVATE /EHsc /W4 /utf-8 /U_DEBUG)
target_compile_definitions(soui_host_visibility_source_tests PRIVATE NDEBUG NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_include_directories(soui_host_visibility_source_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_property(TARGET soui_host_visibility_source_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
add_test(NAME kernel.native.host-visibility-source COMMAND soui_host_visibility_source_tests)
set_tests_properties(kernel.native.host-visibility-source PROPERTIES TIMEOUT 15)

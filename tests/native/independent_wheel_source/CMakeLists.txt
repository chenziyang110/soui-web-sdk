# Compile extracted production bytes; dependency fakes do not establish native/runtime correctness.
find_program(NODE_EXECUTABLE node REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/common/InputFrameTrace.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/common/PaintLifetime.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/input/ScrollController.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/input/ScrollController.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/ScrollTreeSnapshot.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/ScrollTreeSnapshot.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/InputFrameCoverage.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/IndependentInputTrace.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/DrawFrameWakePolicy.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/LegacyPaintRetryPolicy.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/ResourceNotificationPolicy.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/LayerTreeHost.cpp")
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" COMMAND_ERROR_IS_FATAL ANY)
add_executable(soui_independent_wheel_source_tests fixture.cpp)
target_sources(soui_independent_wheel_source_tests PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated/cc/input/ScrollController.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/cc/trees/ScrollTreeSnapshot.cpp")
target_compile_features(soui_independent_wheel_source_tests PRIVATE cxx_std_17)
target_compile_options(soui_independent_wheel_source_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
target_compile_definitions(soui_independent_wheel_source_tests PRIVATE NDEBUG NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_include_directories(soui_independent_wheel_source_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_property(TARGET soui_independent_wheel_source_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
add_test(NAME kernel.native.independent-wheel-source COMMAND soui_independent_wheel_source_tests)
set_tests_properties(kernel.native.independent-wheel-source PROPERTIES TIMEOUT 15)

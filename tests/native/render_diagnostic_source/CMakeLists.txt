find_program(NODE_EXECUTABLE node REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/content/common/InputFrameTrace.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/RenderDiagnosticTrace.h"
  "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink/cc/trees/GpuSkiaSurface.cpp")
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.cjs"
  "${SOUI_DISTRIBUTION_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/generated" COMMAND_ERROR_IS_FATAL ANY)
add_executable(soui_render_diagnostic_source_tests fixture.cpp)
target_compile_features(soui_render_diagnostic_source_tests PRIVATE cxx_std_17)
target_compile_options(soui_render_diagnostic_source_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
target_compile_definitions(soui_render_diagnostic_source_tests PRIVATE NDEBUG NOMINMAX)
target_include_directories(soui_render_diagnostic_source_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_property(TARGET soui_render_diagnostic_source_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
add_test(NAME kernel.native.render-diagnostic-source COMMAND soui_render_diagnostic_source_tests)
set_tests_properties(kernel.native.render-diagnostic-source PROPERTIES TIMEOUT 15)

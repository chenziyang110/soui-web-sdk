# Source contracts compile the current headers, never a historical kernel LIB.
function(soui_input_ui_policy_test target source test_name)
  add_executable(${target} "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
  target_include_directories(${target} PRIVATE
    "${SOUI_DISTRIBUTION_ROOT}/sdk/include"
    "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/miniblink")
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE /utf-8 /W4 /U_DEBUG)
  target_compile_definitions(${target} PRIVATE NDEBUG NOMINMAX)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  add_test(NAME ${test_name} COMMAND ${target})
  set_tests_properties(${test_name} PROPERTIES TIMEOUT 15)
endfunction()

add_subdirectory(render_deadline_policy)
soui_input_ui_policy_test(soui_fractional_frame_phase_tests fractional_frame_phase_tests.cpp
  kernel.native.fractional-frame-phase)
soui_input_ui_policy_test(soui_resource_notification_policy_tests resource_notification_policy_tests.cpp
  kernel.native.resource-notification-policy)

# Included by tests/native; the real SDK owns the endpoint implementation.
add_executable(soui_windows_local_endpoint_tests endpoint_tests.cpp)
target_link_libraries(soui_windows_local_endpoint_tests PRIVATE Soui::Sdk Soui::ActionJson)
target_compile_features(soui_windows_local_endpoint_tests PRIVATE cxx_std_17)
target_compile_options(soui_windows_local_endpoint_tests PRIVATE /utf-8 /W4 /U_DEBUG)
target_compile_definitions(soui_windows_local_endpoint_tests PRIVATE NDEBUG _CRT_SECURE_NO_WARNINGS)
set_property(TARGET soui_windows_local_endpoint_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

set(SOUI_ENDPOINT_CASES
  invalid_options default_deny handshake_required owner_dispatch wire_credentials
  frame_bounds consecutive_requests fragmented_frame external_process duplicate_fields navigation_revoke reconnect
  callback_throw authorizer_throw issue_close_race lease_expiry partial_read_stop
  owner_timeout callback_stop security_first_instance)
foreach(SOUI_ENDPOINT_CASE IN LISTS SOUI_ENDPOINT_CASES)
  add_test(NAME "windows_local_endpoint_${SOUI_ENDPOINT_CASE}"
    COMMAND soui_windows_local_endpoint_tests "${SOUI_ENDPOINT_CASE}")
  set_tests_properties("windows_local_endpoint_${SOUI_ENDPOINT_CASE}" PROPERTIES TIMEOUT 15)
endforeach()

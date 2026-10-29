add_executable(soui_native_trace_ui_lease_tests fixture.cpp)
target_link_libraries(soui_native_trace_ui_lease_tests PRIVATE Soui::Sdk)
target_compile_features(soui_native_trace_ui_lease_tests PRIVATE cxx_std_17)
target_compile_definitions(soui_native_trace_ui_lease_tests PRIVATE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
add_test(NAME sdk.native.native-trace-ui-lease COMMAND soui_native_trace_ui_lease_tests)

target_compile_options(soui_native_trace_ui_lease_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
set_property(TARGET soui_native_trace_ui_lease_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set_tests_properties(sdk.native.native-trace-ui-lease PROPERTIES TIMEOUT 15)

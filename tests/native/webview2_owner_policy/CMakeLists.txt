add_executable(soui_webview2_owner_policy_tests fixture.cpp)
target_compile_features(soui_webview2_owner_policy_tests PRIVATE cxx_std_17)
add_test(NAME sdk.native.webview2-owner-policy COMMAND soui_webview2_owner_policy_tests)

target_compile_options(soui_webview2_owner_policy_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
set_property(TARGET soui_webview2_owner_policy_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set_tests_properties(sdk.native.webview2-owner-policy PROPERTIES TIMEOUT 15)

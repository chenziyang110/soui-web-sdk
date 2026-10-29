add_executable(soui_host_frame_policy_tests fixture.cpp)
target_compile_features(soui_host_frame_policy_tests PRIVATE cxx_std_17)
add_test(NAME sdk.native.host-frame-policy COMMAND soui_host_frame_policy_tests)

target_compile_options(soui_host_frame_policy_tests PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
set_property(TARGET soui_host_frame_policy_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set_tests_properties(sdk.native.host-frame-policy PROPERTIES TIMEOUT 15)

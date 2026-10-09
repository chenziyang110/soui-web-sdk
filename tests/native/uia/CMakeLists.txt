# Candidate opt-in native test registration, Root integrates after source review.
add_executable(soui_windows_uia_provider_tests ../windows_uia_provider_tests.cpp)
target_compile_features(soui_windows_uia_provider_tests PRIVATE cxx_std_17)
target_compile_options(soui_windows_uia_provider_tests PRIVATE /utf-8 /W4 /U_DEBUG)
set_property(TARGET soui_windows_uia_provider_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_include_directories(soui_windows_uia_provider_tests PRIVATE "${SOUI_DISTRIBUTION_ROOT}/third_party/include/json")
target_link_libraries(soui_windows_uia_provider_tests PRIVATE Soui::Uia gdi32)
foreach(uia_case properties mutations stale-secret bounded callback-close uia-client com-initialization api-close)
  add_test(NAME sdk.native.uia.${uia_case} COMMAND soui_windows_uia_provider_tests ${uia_case})
  set_tests_properties(sdk.native.uia.${uia_case} PROPERTIES TIMEOUT 15)
endforeach()
# Test exe uses COM activation of CUIAutomation, never Uiautomationcore.lib.

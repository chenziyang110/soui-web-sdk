# Normal SDK production-owner tests. No copied SDK TUs or test seam macros.
if(NOT TARGET Soui::Sdk)
  message(FATAL_ERROR "runtime_metrics tests require the normal Soui::Sdk target")
endif()
if(NOT TARGET Soui::ActionJson)
  message(FATAL_ERROR "runtime_metrics tests require the public ActionJson dependency")
endif()
add_executable(soui_runtime_metrics_tests runtime_metrics_tests.cpp)
target_compile_features(soui_runtime_metrics_tests PRIVATE cxx_std_17)
target_compile_options(soui_runtime_metrics_tests PRIVATE /W4 /EHsc /utf-8 /U_DEBUG)
set_property(TARGET soui_runtime_metrics_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_libraries(soui_runtime_metrics_tests PRIVATE Soui::Sdk Soui::ActionJson)
set(runtime_metrics_cases
  disabled-epoch task-capacity task-subscriptions task-retention
  permission-aliases permission-owners engine-fifo software-lanes
  gui-facade software-owner scalar-boundaries)
foreach(case IN LISTS runtime_metrics_cases)
  add_test(NAME sdk.native.runtime-metrics.${case} COMMAND soui_runtime_metrics_tests ${case})
  set_tests_properties(sdk.native.runtime-metrics.${case} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endforeach()

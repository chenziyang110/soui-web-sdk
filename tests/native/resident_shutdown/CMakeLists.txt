cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiResidentShutdownPolicy LANGUAGES CXX)
  enable_testing()
endif()
add_executable(soui_resident_shutdown_policy_tests policy_tests.cpp)
target_compile_features(soui_resident_shutdown_policy_tests PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(soui_resident_shutdown_policy_tests PRIVATE /utf-8 /W4 /U_DEBUG)
  set_property(TARGET soui_resident_shutdown_policy_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
endif()
set(SOUI_RESIDENT_SHUTDOWN_POLICY_CASES
  inactive zero-tick repeated-exit pending-boundary tick-wrap clock-regression
  exact-pending unknown-close completed-close)
foreach(policy_case IN LISTS SOUI_RESIDENT_SHUTDOWN_POLICY_CASES)
  add_test(NAME "demo.resident-shutdown-policy.${policy_case}"
    COMMAND soui_resident_shutdown_policy_tests "${policy_case}")
  set_tests_properties("demo.resident-shutdown-policy.${policy_case}" PROPERTIES TIMEOUT 10)
endforeach()

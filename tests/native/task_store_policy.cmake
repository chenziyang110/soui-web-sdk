# Regressions link the selected normal SDK; source alone is not execution evidence.
add_executable(soui_task_store_policy_bridge_tests
  "${CMAKE_CURRENT_LIST_DIR}/task_store_policy_bridge_tests.cpp")
add_executable(soui_task_store_policy_pool_tests
  "${CMAKE_CURRENT_LIST_DIR}/task_store_policy_pool_tests.cpp")
add_executable(soui_task_store_default_quota_tests
  "${CMAKE_CURRENT_LIST_DIR}/task_store_default_quota_tests.cpp")
foreach(target soui_task_store_policy_bridge_tests soui_task_store_policy_pool_tests soui_task_store_default_quota_tests)
  target_link_libraries(${target} PRIVATE Soui::Sdk Soui::ActionJson)
  target_include_directories(${target} BEFORE PRIVATE "${SOUI_DISTRIBUTION_ROOT}/sdk/include")
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE /utf-8 /W4 /U_DEBUG)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO
    "/MAP:$<TARGET_FILE_DIR:${target}>/${target}-$<CONFIG>.map")
endforeach()
# Link MAP/object/archive identity must prove the actual current Soui::Sdk Store,
# service and Bridge for the public target. The primitive target is no such proof.
add_test(NAME sdk.native.task-store-policy-bridge COMMAND soui_task_store_policy_bridge_tests)
add_test(NAME sdk.native.task-store-policy-pool-controls COMMAND soui_task_store_policy_pool_tests)
set_tests_properties(sdk.native.task-store-policy-bridge PROPERTIES TIMEOUT 120)
set_tests_properties(sdk.native.task-store-policy-pool-controls PROPERTIES TIMEOUT 15)
add_test(NAME sdk.native.task-store-default-quota COMMAND soui_task_store_default_quota_tests)
set_tests_properties(sdk.native.task-store-default-quota PROPERTIES TIMEOUT 30)

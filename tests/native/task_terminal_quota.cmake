# Add to an independently admitted SDK build after overlaying this candidate's
# headers/Store/service. This is not a configure/execute controller.
# The normal SDK and fault target must see the same candidate public headers.
add_executable(soui_task_terminal_quota_tests
  "${CMAKE_CURRENT_LIST_DIR}/task_terminal_quota_tests.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/sdk/src/runtime/core/task/task_store.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/sdk/src/runtime/services/task/task_service.cpp"
  "${SOUI_DISTRIBUTION_ROOT}/sdk/src/runtime/core/result/result.cpp")
target_link_libraries(soui_task_terminal_quota_tests PRIVATE Soui::Sdk Soui::ActionJson)
target_include_directories(soui_task_terminal_quota_tests BEFORE PRIVATE "${SOUI_DISTRIBUTION_ROOT}/sdk/include")
target_compile_features(soui_task_terminal_quota_tests PRIVATE cxx_std_17)
target_compile_definitions(soui_task_terminal_quota_tests PRIVATE
  SOUI_TASK_STORE_STORAGE_TEST_ONLY SOUI_TASK_TERMINAL_QUOTA_TEST_ONLY)
target_compile_options(soui_task_terminal_quota_tests PRIVATE /utf-8 /W4 /U_DEBUG)
set_property(TARGET soui_task_terminal_quota_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
add_test(NAME sdk.native.task-terminal-quota COMMAND soui_task_terminal_quota_tests)
set_tests_properties(sdk.native.task-terminal-quota PROPERTIES TIMEOUT 90)

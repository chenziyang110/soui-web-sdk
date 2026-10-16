# Source-only recipe. Root integrates this after genuine SDK Source admission.
if(NOT TARGET Soui::Sdk OR NOT TARGET Soui::ActionJson OR NOT DEFINED SOUI_DISTRIBUTION_ROOT)
  message(FATAL_ERROR "Typed quota tests require the actual admitted SDK and its source root")
endif()
add_executable(soui_task_store_quota_typed_gui_tests
  "${CMAKE_CURRENT_LIST_DIR}/task_store_quota_typed_gui_tests.cpp")
target_link_libraries(soui_task_store_quota_typed_gui_tests PRIVATE Soui::Sdk Soui::ActionJson)
target_include_directories(soui_task_store_quota_typed_gui_tests BEFORE PRIVATE
  "${SOUI_DISTRIBUTION_ROOT}/tests/native" "${SOUI_DISTRIBUTION_ROOT}/sdk/include")
target_compile_features(soui_task_store_quota_typed_gui_tests PRIVATE cxx_std_17)
target_compile_options(soui_task_store_quota_typed_gui_tests PRIVATE /utf-8 /W4 /U_DEBUG)
set_property(TARGET soui_task_store_quota_typed_gui_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_options(soui_task_store_quota_typed_gui_tests PRIVATE /INCREMENTAL:NO
  "/MAP:$<TARGET_FILE_DIR:soui_task_store_quota_typed_gui_tests>/soui_task_store_quota_typed_gui_tests-$<CONFIG>.map")
foreach(quota_case IN ITEMS typed-local-quota gui-prepare-quota)
  add_test(NAME "task_store_quota_${quota_case}"
    COMMAND soui_task_store_quota_typed_gui_tests "${quota_case}")
  set_tests_properties("task_store_quota_${quota_case}" PROPERTIES TIMEOUT 60)
endforeach()
# No Store/service/Bridge CPP, private hook macro, CMake execution or DLL copy.
# Actual new SDK archive extraction/source/DB/compiler/object/MAP proof is pending.

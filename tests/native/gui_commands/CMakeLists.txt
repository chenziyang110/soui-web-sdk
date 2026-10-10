add_executable(soui_gui_command_tests gui_command_tests.cpp)
target_link_libraries(soui_gui_command_tests PRIVATE Soui::Sdk Soui::ActionJson)
target_compile_features(soui_gui_command_tests PRIVATE cxx_std_17)
target_compile_options(soui_gui_command_tests PRIVATE /utf-8 /W4 /U_DEBUG)
target_compile_definitions(soui_gui_command_tests PRIVATE NDEBUG _CRT_SECURE_NO_WARNINGS)
set_property(TARGET soui_gui_command_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(SOUI_GUI_COMMAND_CASES registration scope-matrix one-use-effect actual-child owner-reentrancy
  revoke-prebind timeout-consumption capacity-secret two-parent-pool external-peer
  foreign-owner-effect queued-effect-expiry queued-effect-proof-close negative-ack-unused)
foreach(SOUI_GUI_COMMAND_CASE IN LISTS SOUI_GUI_COMMAND_CASES)
  add_test(NAME "gui_command_${SOUI_GUI_COMMAND_CASE}" COMMAND soui_gui_command_tests "${SOUI_GUI_COMMAND_CASE}")
  set_tests_properties("gui_command_${SOUI_GUI_COMMAND_CASE}" PROPERTIES TIMEOUT 15)
endforeach()

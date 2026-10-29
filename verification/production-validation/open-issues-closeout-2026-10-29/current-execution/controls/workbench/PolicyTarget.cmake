# Output-only current policy target, injected after this consumer's project().
# It does not change the frozen Workbench recipe or build an SDK target.
if(SOUI_WORKBENCH_ARCH STREQUAL "x64")
  if(NOT IS_DIRECTORY "${SOUI_CURRENT_POLICY_ROOT}/tests/native/resident_shutdown")
    message(FATAL_ERROR "The exact current policy source tree is required")
  endif()
  add_subdirectory("${SOUI_CURRENT_POLICY_ROOT}/tests/native/resident_shutdown"
    "${CMAKE_BINARY_DIR}/policy-current")
endif()

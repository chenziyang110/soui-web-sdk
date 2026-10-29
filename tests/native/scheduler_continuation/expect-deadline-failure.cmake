cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED EXECUTABLE OR NOT FAILURE MATCHES "^(close|unregister)$")
  message(FATAL_ERROR "Expected executable and exact deadline failure case")
endif()
# Compatibility entry for older callers. The current production APIs are void;
# their equivalent fence assertions are drain/close ordering, not BOOL failures.
if(FAILURE STREQUAL "close")
  set(boundary "close-order")
else()
  set(boundary "callback-drain")
endif()
execute_process(COMMAND "${EXECUTABLE}" "--${boundary}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Deadline ${boundary} contract failed: ${result} ${output} ${error}")
endif()

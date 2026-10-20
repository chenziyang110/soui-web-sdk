add_executable(soui_runtime_integration runtime_integration.cpp)
target_link_libraries(soui_runtime_integration PRIVATE Soui::Runtime)
target_include_directories(soui_runtime_integration PRIVATE "${SOUI_DISTRIBUTION_ROOT}/third_party/include/json")
add_test(NAME sdk.portable.integration COMMAND soui_runtime_integration)
set_tests_properties(sdk.portable.integration PROPERTIES TIMEOUT 60)

add_executable(soui_runtime_primitives runtime_primitives.cpp)
target_link_libraries(soui_runtime_primitives PRIVATE Soui::Runtime)
add_test(NAME sdk.portable.primitives COMMAND soui_runtime_primitives)
set_tests_properties(sdk.portable.primitives PROPERTIES TIMEOUT 30)
foreach(target soui_runtime_integration soui_runtime_primitives)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /W4)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()

configure_file(installed-consumer.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/installed-consumer.cmake" @ONLY)
add_test(NAME sdk.portable.installed-consumer
  COMMAND "${CMAKE_COMMAND}" "-DCONFIG=$<CONFIG>" -P "${CMAKE_CURRENT_BINARY_DIR}/installed-consumer.cmake")
set_tests_properties(sdk.portable.installed-consumer PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

find_program(SOUI_PORTABLE_NODE_EXECUTABLE NAMES node node.exe)
if(SOUI_PORTABLE_NODE_EXECUTABLE)
  add_test(NAME sdk.portable.platform-matrix
    COMMAND "${SOUI_PORTABLE_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/platform-matrix.test.cjs")
  set_tests_properties(sdk.portable.platform-matrix PROPERTIES TIMEOUT 60
    ENVIRONMENT "CMAKE_COMMAND=${CMAKE_COMMAND}")
endif()

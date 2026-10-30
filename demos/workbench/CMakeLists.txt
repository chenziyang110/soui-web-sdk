soui_add_application(SouiWorkbench ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES app/main.cpp integration/demo_bridge_adapter.cpp
    integration/native_media_preview.cpp integration/native_media_encoder.cpp)

# FileInsight samples are native filesystem data, outside the HTTP resource root.
find_program(SOUI_WORKBENCH_NODE_EXECUTABLE node.exe REQUIRED)
add_custom_target(SouiWorkbench_native_fixtures
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/SouiWorkbench"
  COMMAND "${SOUI_WORKBENCH_NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/deploy-native-fixtures.cjs"
    --source "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    --application "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/SouiWorkbench"
  VERBATIM)
add_dependencies(SouiWorkbench SouiWorkbench_native_fixtures)

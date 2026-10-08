if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
  project(SouiStarter VERSION 0.1.0 LANGUAGES C CXX RC)
  add_subdirectory(sdk)
  include(cmake/SouiApplication.cmake)
endif()
soui_add_application(SouiStarter ROOT "${CMAKE_CURRENT_SOURCE_DIR}" SOURCES app/main.cpp)
target_link_libraries(SouiStarter PRIVATE Soui::ActionJson)
add_custom_target(SouiStarter_actions_check ALL
  COMMAND "${SOUI_NODE_EXECUTABLE}" "${SOUI_DISTRIBUTION_ROOT}/tools/sdk/actiongen.cjs"
    --source "${CMAKE_CURRENT_SOURCE_DIR}/contracts/actions.json" --root "${CMAKE_CURRENT_SOURCE_DIR}" --check
  VERBATIM)
add_dependencies(SouiStarter SouiStarter_actions_check)

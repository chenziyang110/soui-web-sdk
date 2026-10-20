cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiUnifiedDesktopExample LANGUAGES CXX)
  find_package(SouiDesktop REQUIRED CONFIG)
endif()
add_executable(soui_desktop_example main.cpp)
target_compile_features(soui_desktop_example PRIVATE cxx_std_17)
target_link_libraries(soui_desktop_example PRIVATE Soui::Desktop)
if(MSVC)
  target_compile_options(soui_desktop_example PRIVATE /utf-8)
  set_property(TARGET soui_desktop_example PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
endif()
add_custom_command(TARGET soui_desktop_example POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/web"
    "$<TARGET_FILE_DIR:soui_desktop_example>/web")

option(SOUI_DESKTOP_STAGE_EXAMPLE "Enable explicit unsigned example application staging target" OFF)
if(SOUI_DESKTOP_STAGE_EXAMPLE)
  soui_stage_application(TARGET soui_desktop_example NAME soui-example
    ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/web"
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/staged-$<CONFIG>"
    VERSION "0.1.0" IDENTIFIER "org.soui.example")
endif()

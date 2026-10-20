# These tests use the real common application/protocol with a fake backend.
# They never initialize a renderer, clipboard, file picker, or native window.
add_executable(soui_desktop_contract
  contract.cpp
  ../../sdk/src/desktop/assets.cpp
  ../../sdk/src/desktop/application.cpp
  ../../sdk/src/desktop/protocol.cpp)
target_link_libraries(soui_desktop_contract PRIVATE Soui::Runtime)
target_include_directories(soui_desktop_contract PRIVATE
  "${SOUI_DISTRIBUTION_ROOT}/third_party/include/json")
target_compile_features(soui_desktop_contract PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(soui_desktop_contract PRIVATE /utf-8 /W4)
  set_property(TARGET soui_desktop_contract PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else()
  target_compile_options(soui_desktop_contract PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(soui_desktop_assets assets.cpp ../../sdk/src/desktop/assets.cpp)
target_compile_features(soui_desktop_assets PRIVATE cxx_std_17)
target_include_directories(soui_desktop_assets PRIVATE "${SOUI_DISTRIBUTION_ROOT}/sdk/include")
if(MSVC)
  target_compile_options(soui_desktop_assets PRIVATE /utf-8 /W4)
  set_property(TARGET soui_desktop_assets PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else()
  target_compile_options(soui_desktop_assets PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME sdk.desktop.assets COMMAND soui_desktop_assets)
set_tests_properties(sdk.desktop.assets PROPERTIES TIMEOUT 30 LABELS "desktop;contract;no-gui")

if(SOUI_BUILD_DESKTOP AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # sdk/desktop's imported pkg-config target has directory scope. The fixture
  # includes GTK/WebKit directly and needs its own dependency in this directory.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SOUI_DESKTOP_TEST_WEBKIT REQUIRED IMPORTED_TARGET
    "webkit2gtk-4.1>=2.40" "gtk+-3.0>=3.24")
  add_executable(soui_desktop_linux_integration ../portable/desktop_linux_backend.cpp)
  target_link_libraries(soui_desktop_linux_integration PRIVATE Soui::Desktop PkgConfig::SOUI_DESKTOP_TEST_WEBKIT)
  target_compile_features(soui_desktop_linux_integration PRIVATE cxx_std_17)
  target_compile_options(soui_desktop_linux_integration PRIVATE -Wall -Wextra -Wpedantic)
  option(SOUI_DESKTOP_GUI_TESTS "Register tests requiring a real display and native desktop capabilities" OFF)
  if(SOUI_DESKTOP_GUI_TESTS)
    add_test(NAME sdk.desktop.linux-native COMMAND soui_desktop_linux_integration)
    # Three native phases each have a 20-second watchdog, plus setup/teardown.
    set_tests_properties(sdk.desktop.linux-native PROPERTIES TIMEOUT 70 LABELS "desktop;native-gui")
  endif()
endif()
add_test(NAME sdk.desktop.contract COMMAND soui_desktop_contract)
set_tests_properties(sdk.desktop.contract PROPERTIES TIMEOUT 30 LABELS "desktop;contract;no-gui")

find_program(SOUI_DESKTOP_NODE_EXECUTABLE NAMES node node.exe)
if(SOUI_DESKTOP_NODE_EXECUTABLE)
  add_test(NAME sdk.desktop.preload
    COMMAND "${CMAKE_COMMAND}" -E env "SOUI_DESKTOP_CONTRACT_EXECUTABLE=$<TARGET_FILE:soui_desktop_contract>"
      "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/preload.test.cjs")
  set_tests_properties(sdk.desktop.preload PROPERTIES TIMEOUT 30 LABELS "desktop;contract;no-gui")
  add_test(NAME sdk.desktop.package-tool
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/package.test.cjs")
  set_tests_properties(sdk.desktop.package-tool PROPERTIES TIMEOUT 30 LABELS "desktop;tooling;no-gui")
endif()

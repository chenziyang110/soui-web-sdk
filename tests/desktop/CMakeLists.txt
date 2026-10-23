# These tests use the real common application/protocol with a fake backend.
# They never initialize a renderer, clipboard, file picker, or native window.
add_executable(soui_desktop_contract
  contract.cpp
  ../../sdk/src/desktop/assets.cpp
  ../../sdk/src/desktop/asset_executor.cpp
  ../../sdk/src/desktop/resource_server.cpp
  ../../sdk/src/desktop/application.cpp
  ../../sdk/src/desktop/protocol.cpp)
target_link_libraries(soui_desktop_contract PRIVATE Soui::Runtime)
if(WIN32)
  target_link_libraries(soui_desktop_contract PRIVATE ws2_32)
endif()
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
add_executable(soui_desktop_asset_executor asset_executor.cpp
  ../../sdk/src/desktop/asset_executor.cpp ../../sdk/src/desktop/assets.cpp)
target_link_libraries(soui_desktop_asset_executor PRIVATE Soui::Runtime)
target_compile_features(soui_desktop_asset_executor PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(soui_desktop_asset_executor PRIVATE /utf-8 /W4)
  set_property(TARGET soui_desktop_asset_executor PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else()
  target_compile_options(soui_desktop_asset_executor PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME sdk.desktop.asset-executor COMMAND soui_desktop_asset_executor)
set_tests_properties(sdk.desktop.asset-executor PROPERTIES TIMEOUT 30 LABELS "desktop;contract;no-gui")

# Includes the production service to inject one connection-local accept failure.
add_executable(soui_desktop_resource_server resource_server.cpp)
target_link_libraries(soui_desktop_resource_server PRIVATE Soui::Runtime)
target_compile_features(soui_desktop_resource_server PRIVATE cxx_std_17)
if(WIN32)
  target_link_libraries(soui_desktop_resource_server PRIVATE ws2_32)
endif()
if(MSVC)
  target_compile_options(soui_desktop_resource_server PRIVATE /utf-8 /W4)
  set_property(TARGET soui_desktop_resource_server PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else()
  target_compile_options(soui_desktop_resource_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME sdk.desktop.resource-server COMMAND soui_desktop_resource_server)
set_tests_properties(sdk.desktop.resource-server PROPERTIES TIMEOUT 10 LABELS "desktop;contract;no-gui")

if(SOUI_BUILD_DESKTOP AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(soui_desktop_linux_runtime_preflight linux_runtime_preflight.cpp)
  target_compile_features(soui_desktop_linux_runtime_preflight PRIVATE cxx_std_17)
  target_include_directories(soui_desktop_linux_runtime_preflight PRIVATE
    "${SOUI_DISTRIBUTION_ROOT}/sdk/include")
  target_compile_options(soui_desktop_linux_runtime_preflight PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sdk.desktop.linux-runtime-preflight COMMAND soui_desktop_linux_runtime_preflight)
  set_tests_properties(sdk.desktop.linux-runtime-preflight PROPERTIES TIMEOUT 10 LABELS "desktop;contract;no-gui")
  # sdk/desktop's imported pkg-config target has directory scope. The fixture
  # includes GTK/WebKit directly and needs its own dependency in this directory.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SOUI_DESKTOP_TEST_WEBKIT REQUIRED IMPORTED_TARGET
    "webkit2gtk-4.1>=2.40" "gtk+-3.0>=3.24")
  # Includes production callback code, but never creates GTK/WebKit objects.
  # Do not link Soui::Desktop: that would duplicate the backend implementation.
  add_executable(soui_desktop_linux_document_lifecycle linux_document_lifecycle.cpp)
  target_link_libraries(soui_desktop_linux_document_lifecycle PRIVATE PkgConfig::SOUI_DESKTOP_TEST_WEBKIT)
  target_include_directories(soui_desktop_linux_document_lifecycle PRIVATE
    "${SOUI_DISTRIBUTION_ROOT}/sdk/include")
  target_compile_features(soui_desktop_linux_document_lifecycle PRIVATE cxx_std_17)
  target_compile_options(soui_desktop_linux_document_lifecycle PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sdk.desktop.linux-document-lifecycle COMMAND soui_desktop_linux_document_lifecycle)
  set_tests_properties(sdk.desktop.linux-document-lifecycle PROPERTIES TIMEOUT 10 LABELS "desktop;contract;no-gui")
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
  # Native ELF tooling probe only; never executes the linked applications.
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
    find_program(SOUI_DESKTOP_TEST_NINJA NAMES ninja ninja-build)
    if(SOUI_DESKTOP_TEST_NINJA)
      add_test(NAME sdk.desktop.rpath-policy
        COMMAND "${CMAKE_COMMAND}" -E env "CMAKE=${CMAKE_COMMAND}" "NINJA=${SOUI_DESKTOP_TEST_NINJA}"
          "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/rpath-policy.test.cjs")
      set_tests_properties(sdk.desktop.rpath-policy PROPERTIES TIMEOUT 120 LABELS "desktop;tooling;no-gui")
      add_test(NAME sdk.desktop.multiconfig-delivery
        COMMAND "${CMAKE_COMMAND}" -E env "CMAKE=${CMAKE_COMMAND}" "NINJA=${SOUI_DESKTOP_TEST_NINJA}"
          "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/multiconfig-delivery.test.cjs")
      set_tests_properties(sdk.desktop.multiconfig-delivery PROPERTIES TIMEOUT 120 LABELS "desktop;tooling;no-gui")
      add_test(NAME sdk.desktop.consumer-architecture
        COMMAND "${CMAKE_COMMAND}" -E env "CMAKE=${CMAKE_COMMAND}" "NINJA=${SOUI_DESKTOP_TEST_NINJA}"
          "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/consumer-architecture.test.cjs")
      set_tests_properties(sdk.desktop.consumer-architecture PROPERTIES TIMEOUT 120 LABELS "desktop;tooling;no-gui")
    endif()
  endif()
  add_test(NAME sdk.desktop.sync-assets
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/sync-assets.test.cjs")
  set_tests_properties(sdk.desktop.sync-assets PROPERTIES TIMEOUT 30 LABELS "desktop;tooling;no-gui")
  add_test(NAME sdk.desktop.frontend-adapter
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/frontend-adapter.test.mjs")
  set_tests_properties(sdk.desktop.frontend-adapter PROPERTIES TIMEOUT 30 LABELS "desktop;contract;no-gui")
  add_test(NAME sdk.desktop.preload
    COMMAND "${CMAKE_COMMAND}" -E env "SOUI_DESKTOP_CONTRACT_EXECUTABLE=$<TARGET_FILE:soui_desktop_contract>"
      "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/preload.test.cjs")
  set_tests_properties(sdk.desktop.preload PROPERTIES TIMEOUT 30 LABELS "desktop;contract;no-gui")
  add_test(NAME sdk.desktop.archive-app
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/archive-app.test.cjs")
  set_tests_properties(sdk.desktop.archive-app PROPERTIES TIMEOUT 30 LABELS "desktop;tooling;no-gui")
  add_test(NAME sdk.desktop.verify-app
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/verify-app.test.cjs")
  set_tests_properties(sdk.desktop.verify-app PROPERTIES TIMEOUT 30 LABELS "desktop;tooling;no-gui")
  add_test(NAME sdk.desktop.create-app
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/create-app.test.cjs")
  set_tests_properties(sdk.desktop.create-app PROPERTIES TIMEOUT 30 LABELS "desktop;tooling;no-gui")
  add_test(NAME sdk.desktop.runtime-preflight
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/preflight.test.cjs")
  set_tests_properties(sdk.desktop.runtime-preflight PROPERTIES TIMEOUT 30 LABELS "desktop;tooling;no-gui")
  add_test(NAME sdk.desktop.application-stage
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/stage-app.test.cjs")
  set_tests_properties(sdk.desktop.application-stage PROPERTIES TIMEOUT 30 LABELS "desktop;tooling;no-gui")
  add_test(NAME sdk.desktop.package-tool
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/package.test.cjs")
  set_tests_properties(sdk.desktop.package-tool PROPERTIES TIMEOUT 30 LABELS "desktop;tooling;no-gui")
endif()

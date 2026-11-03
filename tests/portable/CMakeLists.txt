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
  add_test(NAME sdk.portable.platform-matrix-cli
    COMMAND "${SOUI_PORTABLE_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/platform-matrix-cli.test.cjs")
  set_tests_properties(sdk.portable.platform-matrix-cli PROPERTIES TIMEOUT 10)
  set(_soui_platform_matrix_setup sdk.portable.platform-matrix-cli)
  function(_soui_platform_matrix_group suffix timeout pattern)
    set(_name "sdk.portable.platform-matrix-${suffix}")
    add_test(NAME "${_name}"
      COMMAND "${SOUI_PORTABLE_NODE_EXECUTABLE}" --test "--test-name-pattern=${pattern}"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform-matrix.test.cjs")
    set_tests_properties("${_name}" PROPERTIES TIMEOUT "${timeout}"
      ENVIRONMENT "CMAKE_COMMAND=${CMAKE_COMMAND}")
    set(_soui_platform_matrix_setup "${_soui_platform_matrix_setup};${_name}" PARENT_SCOPE)
  endfunction()

  # Each group allows discovery (30s), its child timeout sum, and 20s overhead.
  # Script calls retain 30s each; the two probes each allow 10+120+120 seconds.
  _soui_platform_matrix_group(policy 100 "^(architecture aliases preserve ISA rather than infer x64 from pointer width|operating-system aliases normalize without accepting different target systems|unknown architectures and pointer-only guesses fail closed|ARM64EC is rejected in every target hint|pointer-size mismatch rejects x32, AArch64 ILP32 and incorrect declarations|cross-compilation uses target hints rather than the separate host processor|conflicting explicit architecture hints fail even if pointer size agrees|Windows explicit compiler targets can differ from the CMake host processor|macOS universal2 is canonical and list-order independent|macOS 32-bit targets are explicitly unsupported|desktop and binary dependencies reject all non-Windows x86/x64 targets|matrix records policy, unprobed toolchains and no invented validation|current-platform information identifies Node process and unprobed compiler|system WebView source policy is distinct from the legacy desktop binaries|CLI produces JSON and fails unsupported requirements with nonzero status|CLI rejects ambiguous/missing arguments and ignored probe options|missing local CMake reports unavailable without downloading tools)$")
  _soui_platform_matrix_group(cmake-aliases 200 "^CMake synthetic target variables normalize aliases and preserve ARM64$")
  _soui_platform_matrix_group(cmake-cross 110 "^CMake cross builds use declared target rather than host processor$")
  _soui_platform_matrix_group(cmake-windows 260 "^CMake Windows VS and compiler architecture selections override the host processor$")
  _soui_platform_matrix_group(cmake-invalid 380 "^CMake rejects unknown systems/architectures, ARM64EC and pointer conflicts$")
  _soui_platform_matrix_group(cmake-macos 200 "^CMake macOS multi-architecture normalization matches the Node contract$")
  _soui_platform_matrix_group(cmake-dependencies 320 "^CMake dependency requests explicitly fail on unsupported target$")
  _soui_platform_matrix_group(cmake-slices 740 "^CMake macOS compiler identification accepts slice lists without relaxing other hints$")
  _soui_platform_matrix_group(probe-native 300 "^local compiler probe configures and links only its exact requested target$")
  _soui_platform_matrix_group(probe-mismatch 300 "^local compiler probe rejects a mismatched requested target$")
  _soui_platform_matrix_group(linux-cross 170 "^real configure rejects an ARM cross-toolchain that accidentally uses the x64 host compiler$")
  _soui_platform_matrix_group(linux-os 170 "^real compiler OS macros reject a mislabeled CMAKE_SYSTEM_NAME$")
  _soui_platform_matrix_group(linux-arch 170 "^real compiler probes reject a false compiler architecture declaration$")
  _soui_platform_matrix_group(abi 170 "^legacy libstdc[+][+] ABI fails configuration without weakening TaskStore swap guarantees$")

  # Bind the complete list once so the legacy exact selector cannot omit a group.
  set_tests_properties(${_soui_platform_matrix_setup} PROPERTIES
    FIXTURES_SETUP soui_platform_matrix_groups LABELS platform-matrix)
  set_tests_properties(
    sdk.portable.platform-matrix-probe-native sdk.portable.platform-matrix-probe-mismatch
    sdk.portable.platform-matrix-linux-cross sdk.portable.platform-matrix-linux-os
    sdk.portable.platform-matrix-linux-arch sdk.portable.platform-matrix-abi
    PROPERTIES RESOURCE_LOCK soui_platform_compiler)

  # This compatibility entry runs only after every real group and mock passed.
  add_test(NAME sdk.portable.platform-matrix COMMAND "${CMAKE_COMMAND}" -E true)
  set_tests_properties(sdk.portable.platform-matrix PROPERTIES TIMEOUT 5
    FIXTURES_REQUIRED soui_platform_matrix_groups LABELS platform-matrix)
endif()

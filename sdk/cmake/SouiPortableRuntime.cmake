# Explicit source ownership: no Win32 Host, desktop services, endpoint or renderer.
find_package(Threads REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CheckCXXSourceCompiles)
include(CheckCXXSymbolExists)
# These are link-capability checks, even when a cross toolchain uses static
# try-compiles for compiler discovery. No target executable is ever run.
if(DEFINED CMAKE_TRY_COMPILE_TARGET_TYPE)
  set(_soui_had_try_compile_type TRUE)
  set(_soui_saved_try_compile_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
else()
  set(_soui_had_try_compile_type FALSE)
endif()
set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
# The existing TaskStore requires non-throwing same-owner string swaps.
# libstdc++'s legacy copy-on-write ABI does not meet that contract.
unset(SOUI_HAS_SUPPORTED_STRING_ABI CACHE)
check_cxx_source_compiles("#include <string>\n#if defined(__GLIBCXX__) && defined(_GLIBCXX_USE_CXX11_ABI) && !_GLIBCXX_USE_CXX11_ABI\n#error Legacy libstdc++ ABI is unsupported\n#endif\nint main() { return 0; }"
  SOUI_HAS_SUPPORTED_STRING_ABI)
if(NOT SOUI_HAS_SUPPORTED_STRING_ABI)
  message(FATAL_ERROR "Portable Runtime requires a linkable C++17 standard library; libstdc++ must use its modern C++11 ABI (_GLIBCXX_USE_CXX11_ABI=1) for the existing TaskStore non-throwing swap contract.")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  unset(SOUI_HAS_GETRANDOM CACHE)
  check_cxx_symbol_exists(getrandom "sys/random.h" SOUI_HAS_GETRANDOM)
  if(NOT SOUI_HAS_GETRANDOM)
    message(FATAL_ERROR "Portable Runtime requires getrandom from the target Linux libc (sys/random.h); no weak entropy fallback is permitted.")
  endif()
elseif(APPLE)
  find_library(SOUI_SECURITY_FRAMEWORK Security REQUIRED)
endif()

add_library(soui_runtime STATIC
  src/runtime/bridge/action_contract.cpp
  src/runtime/bridge/bridge_transport.cpp
  src/runtime/core/events/event_bus.cpp
  src/runtime/core/result/result.cpp
  src/runtime/core/task/task_store.cpp
  src/runtime/host/session/webview_session.cpp
  src/runtime/platform/secure_random.cpp
  src/runtime/services/permissions/permission_store.cpp
  src/runtime/services/task/task_service.cpp
  src/runtime/services/gui/gui_commands.cpp
  src/runtime/services/filesystem-readonly/directory_scanner.cpp
  src/runtime/services/fileinsight-service/fileinsight-service.cpp)
add_library(Soui::Runtime ALIAS soui_runtime)
set_target_properties(soui_runtime PROPERTIES EXPORT_NAME Runtime)
target_compile_features(soui_runtime PUBLIC cxx_std_17)
target_include_directories(soui_runtime PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")
target_include_directories(soui_runtime PRIVATE "${SOUI_DISTRIBUTION_ROOT}/third_party/include/json")
target_link_libraries(soui_runtime PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(soui_runtime PRIVATE /utf-8 /W4)
  target_compile_definitions(soui_runtime PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0601 _WIN32_WINNT=0x0601)
  set_property(TARGET soui_runtime PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else()
  target_compile_options(soui_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
  target_link_libraries(soui_runtime PRIVATE bcrypt ole32)
elseif(APPLE)
  target_link_libraries(soui_runtime PRIVATE "-framework Security")
endif()

# ARM32 and some other targets need libatomic for 64-bit counters. Probe the
# target linker, never the host architecture, and preserve the dependency on export.
set(_soui_atomic_probe "#include <atomic>\n#include <cstdint>\nint main() { std::atomic<std::uint64_t> n{1}; return static_cast<int>(n.fetch_add(1)); }")
unset(SOUI_HAS_NATIVE_ATOMIC64 CACHE)
check_cxx_source_compiles("${_soui_atomic_probe}" SOUI_HAS_NATIVE_ATOMIC64)
if(NOT SOUI_HAS_NATIVE_ATOMIC64)
  set(_soui_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
  set(CMAKE_REQUIRED_LIBRARIES atomic)
  unset(SOUI_HAS_LIBATOMIC64 CACHE)
  check_cxx_source_compiles("${_soui_atomic_probe}" SOUI_HAS_LIBATOMIC64)
  set(CMAKE_REQUIRED_LIBRARIES "${_soui_saved_required_libraries}")
  if(NOT SOUI_HAS_LIBATOMIC64)
    message(FATAL_ERROR "Portable Runtime requires target 64-bit atomics. Install the target libatomic or select a capable C++17 toolchain.")
  endif()
  target_link_libraries(soui_runtime PUBLIC atomic)
endif()
if(_soui_had_try_compile_type)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_soui_saved_try_compile_type}")
else()
  unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/SouiRuntimeConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/SouiRuntimeConfig.cmake"
  INSTALL_DESTINATION lib/cmake/SouiRuntime)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/SouiRuntimeConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(TARGETS soui_runtime EXPORT SouiRuntimeTargets ARCHIVE DESTINATION lib/$<CONFIG>)
# Export only the implemented subset. Windows-only headers must not appear to
# be usable merely because an installation succeeded on a different platform.
install(FILES include/soui/version.h DESTINATION include/soui)
install(FILES
  include/soui/diagnostics/runtime_metrics.h
  include/soui/diagnostics/renderer_observation_abi.h
  include/soui/diagnostics/renderer_model_abi.h
  include/soui/diagnostics/view_observation_abi.h
  include/soui/diagnostics/storage_observation.h
  include/soui/diagnostics/task_store_private_storage.h
  DESTINATION include/soui/diagnostics)
install(DIRECTORY include/soui/runtime/core/ DESTINATION include/soui/runtime/core)
install(DIRECTORY include/soui/runtime/bridge/ DESTINATION include/soui/runtime/bridge)
install(DIRECTORY include/soui/runtime/host/session/ DESTINATION include/soui/runtime/host/session)
install(FILES include/soui/runtime/host/trusted_document.h DESTINATION include/soui/runtime/host)
foreach(service permissions task gui filesystem-readonly fileinsight-service)
  install(DIRECTORY "include/soui/runtime/services/${service}/"
    DESTINATION "include/soui/runtime/services/${service}")
endforeach()
install(FILES "${SOUI_DISTRIBUTION_ROOT}/LICENSE.md" "${CMAKE_CURRENT_SOURCE_DIR}/portable/THIRD_PARTY_NOTICES.md"
  DESTINATION share/SouiRuntime)
install(EXPORT SouiRuntimeTargets NAMESPACE Soui:: DESTINATION lib/cmake/SouiRuntime)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SouiRuntimeConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/SouiRuntimeConfigVersion.cmake"
  "${SOUI_DISTRIBUTION_ROOT}/cmake/SouiPlatform.cmake" DESTINATION lib/cmake/SouiRuntime)

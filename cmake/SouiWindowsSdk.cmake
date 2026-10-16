if(SOUI_NATIVE_TOOLCHAIN STREQUAL "LLVM22MSVC142")
  if(NOT CMAKE_GENERATOR STREQUAL "Ninja Multi-Config" OR NOT COMMAND soui_llvm22_verify_project
      OR NOT CMAKE_SYSTEM_VERSION STREQUAL "10.0.19041.0")
    message(FATAL_ERROR "LLVM22MSVC142 requires the explicit product toolchain before project()")
  endif()
  # This include runs before language discovery. The generated post-project hook
  # checks the real compiler/ABI; no VS-selected SDK output is assigned here.
  message(STATUS "SOUI explicit LLVM profile selected; real language checks follow project()")
  return()
endif()
# Selection is performed by CMake's generator before this top-level include.
# Check the result; setting this output variable cannot select an SDK.
if(NOT CMAKE_GENERATOR STREQUAL "Visual Studio 17 2022")
  message(FATAL_ERROR "SOUI's fixed SDK configuration requires Visual Studio 17 2022")
endif()
if(NOT "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" STREQUAL "10.0.19041.0")
  message(FATAL_ERROR
    "SOUI requires Windows SDK 10.0.19041.0; CMake selected '${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}'. "
    "Install the exact SDK and use a new generated build directory or explicitly request FreshConfigure.")
endif()
message(STATUS "SOUI verified selected Windows SDK 10.0.19041.0")

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

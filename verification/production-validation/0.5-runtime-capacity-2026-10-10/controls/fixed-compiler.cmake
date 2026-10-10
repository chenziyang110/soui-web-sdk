# Read-only toolchain admission; no copied production TU or target override.
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR
   NOT CMAKE_GENERATOR STREQUAL "Visual Studio 17 2022" OR
   NOT CMAKE_GENERATOR_TOOLSET STREQUAL "v143,host=x64,version=14.44.35207" OR
   NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL "10.0.19041.0" OR
   NOT CMAKE_SYSTEM_VERSION STREQUAL "10.0.19041.0")
  message(FATAL_ERROR "Fixed MSVC14.44.35207 / SDK19041 required")
endif()
if(NOT CMAKE_VS_PLATFORM_NAME STREQUAL SOUI_ROOT_EXPECTED_ARCH)
  message(FATAL_ERROR "Actual generator architecture differs")
endif()
if(SOUI_ROOT_EXPECTED_ARCH STREQUAL "x64")
  set(root_cl_arch x64)
elseif(SOUI_ROOT_EXPECTED_ARCH STREQUAL "Win32")
  set(root_cl_arch x86)
else()
  message(FATAL_ERROR "Only fixed x64 / Win32")
endif()
file(TO_CMAKE_PATH "${CMAKE_CXX_COMPILER}" root_actual_cl)
set(root_expected_cl "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/${root_cl_arch}/cl.exe")
cmake_path(NORMAL_PATH root_actual_cl)
cmake_path(NORMAL_PATH root_expected_cl)
string(TOLOWER "${root_actual_cl}" root_actual_cl)
string(TOLOWER "${root_expected_cl}" root_expected_cl)
if(NOT root_actual_cl STREQUAL root_expected_cl)
  message(FATAL_ERROR "Actual selected compiler differs")
endif()

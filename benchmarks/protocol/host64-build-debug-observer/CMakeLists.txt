cmake_minimum_required(VERSION 3.29)
project(SouiIndependentHost64BuildDebugObserver LANGUAGES CXX)
if(NOT WIN32 OR NOT SOUI_NATIVE_TOOLCHAIN STREQUAL "LLVM22MSVC142" OR
   NOT CMAKE_GENERATOR STREQUAL "Ninja Multi-Config" OR
   NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "22.1.3" OR
   NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR
   NOT CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" OR
   NOT CMAKE_CXX_SIMULATE_VERSION VERSION_EQUAL "19.29" OR
   NOT CMAKE_SYSTEM_VERSION STREQUAL "10.0.19041.0" OR
   NOT SOUI_LLVM_ARCHITECTURE STREQUAL "x64" OR
   NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "x86_64-pc-windows-msvc" OR
   NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Independent observer is AMD64 under the unchanged explicit LLVM22MSVC142/SDK19041 profile")
endif()
get_filename_component(actual_toolchain "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE)
get_filename_component(bound_toolchain "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/toolchains/SouiLLVM22.cmake" ABSOLUTE)
if(NOT actual_toolchain STREQUAL bound_toolchain OR
   NOT CMAKE_CXX_COMPILER STREQUAL "${SOUI_LLVM_ROOT}/bin/clang-cl.exe")
  message(FATAL_ERROR "Frozen current SDK toolchain/compiler identity differs")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(Host64BuildDebugObserver WIN32 ../host64-build-debug-observer.cpp)
target_link_options(Host64BuildDebugObserver PRIVATE /ENTRY:wmainCRTStartup)
target_include_directories(Host64BuildDebugObserver PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/include")
target_compile_features(Host64BuildDebugObserver PRIVATE cxx_std_17)
target_compile_options(Host64BuildDebugObserver PRIVATE /W4 /utf-8)
target_compile_definitions(Host64BuildDebugObserver PRIVATE WINVER=0x0602 _WIN32_WINNT=0x0602 UNICODE _UNICODE)
set_property(TARGET Host64BuildDebugObserver PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_libraries(Host64BuildDebugObserver PRIVATE bcrypt ole32 oleaut32 wbemuuid shell32)

cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/soui/version.h" SOUI_VERSION_DECLARATION REGEX "^#define SOUI_SDK_VERSION_STRING")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" SOUI_PROJECT_VERSION "${SOUI_VERSION_DECLARATION}")
project(SouiSdk VERSION ${SOUI_PROJECT_VERSION} LANGUAGES C CXX)
if(NOT MSVC OR NOT WIN32)
  message(FATAL_ERROR "SouiSdk currently requires Windows and the MSVC v143 toolchain")
endif()
get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(SOUI_DISTRIBUTION_ROOT "${SOUI_DISTRIBUTION_ROOT}" CACHE INTERNAL "SDK distribution root")
file(GLOB_RECURSE SOUI_NATIVE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(soui_sdk STATIC ${SOUI_NATIVE_SOURCES})
add_library(Soui::Sdk ALIAS soui_sdk)
target_compile_features(soui_sdk PUBLIC cxx_std_17)
target_include_directories(soui_sdk PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")
target_compile_definitions(soui_sdk PUBLIC UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_compile_definitions(soui_sdk PRIVATE ENABLE_WKE=0 SOUI_SOURCE_KERNEL_HOST NDEBUG _CRT_SECURE_NO_WARNINGS)
target_compile_options(soui_sdk PRIVATE /utf-8 /W4 /U_DEBUG)
set_property(TARGET soui_sdk PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_libraries(soui_sdk PUBLIC user32 gdi32 imm32 shell32 ole32 oleaut32 uuid comctl32 advapi32 winmm bcrypt)
set_target_properties(soui_sdk PROPERTIES EXPORT_NAME Sdk)

set(ZLIB_SOURCE "${SOUI_DISTRIBUTION_ROOT}/third_party/sources/zlib")
add_library(soui_zlib STATIC ${ZLIB_SOURCE}/adler32.c ${ZLIB_SOURCE}/compress.c
  ${ZLIB_SOURCE}/crc32.c ${ZLIB_SOURCE}/deflate.c ${ZLIB_SOURCE}/infback.c
  ${ZLIB_SOURCE}/inffast.c ${ZLIB_SOURCE}/inflate.c ${ZLIB_SOURCE}/inftrees.c
  ${ZLIB_SOURCE}/trees.c ${ZLIB_SOURCE}/uncompr.c ${ZLIB_SOURCE}/zutil.c)
target_include_directories(soui_zlib PUBLIC "$<BUILD_INTERFACE:${ZLIB_SOURCE}>")
target_compile_definitions(soui_zlib PRIVATE _CRT_SECURE_NO_WARNINGS)
set_property(TARGET soui_zlib PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/SouiSdkConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/SouiSdkConfig.cmake" INSTALL_DESTINATION lib/cmake/SouiSdk)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/SouiSdkConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(TARGETS soui_sdk EXPORT SouiSdkTargets ARCHIVE DESTINATION lib/$<CONFIG>)
install(DIRECTORY include/ DESTINATION include)
install(FILES "${SOUI_DISTRIBUTION_ROOT}/THIRD_PARTY_NOTICES.md" DESTINATION share/SouiSdk)
install(DIRECTORY "${SOUI_DISTRIBUTION_ROOT}/third_party/licenses/" DESTINATION share/SouiSdk/licenses)
install(EXPORT SouiSdkTargets NAMESPACE Soui:: DESTINATION lib/cmake/SouiSdk)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SouiSdkConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/SouiSdkConfigVersion.cmake" DESTINATION lib/cmake/SouiSdk)

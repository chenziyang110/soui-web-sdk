# Candidate opt-in integration only. The distribution root must add_subdirectory
# here under an OFF-by-default SOUI_BUILD_UIA option; this file is outside src GLOB.
add_library(soui_uia STATIC windows_uia_provider.cpp)
add_library(Soui::Uia ALIAS soui_uia)
target_compile_features(soui_uia PUBLIC cxx_std_17)
target_include_directories(soui_uia PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>"
  "$<INSTALL_INTERFACE:include>")
target_compile_definitions(soui_uia PUBLIC UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_compile_options(soui_uia PRIVATE /utf-8 /W4 /U_DEBUG)
set_property(TARGET soui_uia PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_libraries(soui_uia PUBLIC Soui::Sdk ole32 oleaut32 user32)
set_target_properties(soui_uia PROPERTIES EXPORT_NAME Uia)
# No Uiautomationcore.lib. API calls must use controlled dynamic system loading.
install(TARGETS soui_uia EXPORT SouiSdkTargets ARCHIVE DESTINATION lib/$<CONFIG>)

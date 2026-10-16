cmake_minimum_required(VERSION 3.29)
project(SouiProposedUiaInstallation LANGUAGES C CXX)
add_subdirectory("C:/work/sm05-release-050/sdk" sdk)
if(SOUI_BUILD_UIA)
  set_property(TARGET soui_uia PROPERTY SOURCES
    "C:/work/sm05-uia-events-1/proposal/sdk/accessibility/uia/windows_uia_provider.cpp")
  install(FILES "C:/work/sm05-uia-events-1/proposal/sdk/include/soui/runtime/accessibility/windows_uia_provider.h"
    DESTINATION include/soui/runtime/accessibility)
endif()

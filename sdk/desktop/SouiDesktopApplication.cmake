include_guard(GLOBAL)

# Creates an explicit, non-ALL staging target. Re-running it refuses an existing
# output directory; callers choose a new destination for each staged artifact.
function(soui_stage_application)
  cmake_parse_arguments(PARSE_ARGV 0 APP "" "TARGET;NAME;ASSETS;OUTPUT;VERSION;IDENTIFIER" "")
  if(APP_UNPARSED_ARGUMENTS OR APP_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "soui_stage_application received unknown or incomplete arguments")
  endif()
  foreach(required TARGET NAME ASSETS OUTPUT VERSION)
    if(NOT DEFINED APP_${required} OR APP_${required} STREQUAL "")
      message(FATAL_ERROR "soui_stage_application requires ${required}")
    endif()
  endforeach()
  if(NOT TARGET "${APP_TARGET}")
    message(FATAL_ERROR "soui_stage_application TARGET must name an executable")
  endif()
  get_target_property(kind "${APP_TARGET}" TYPE)
  if(NOT kind STREQUAL "EXECUTABLE")
    message(FATAL_ERROR "soui_stage_application TARGET must be an executable")
  endif()
  get_target_property(platform Soui::Desktop SOUI_APPLICATION_PLATFORM)
  get_target_property(architecture Soui::Desktop SOUI_APPLICATION_ARCH)
  if(NOT platform OR NOT architecture)
    message(FATAL_ERROR "soui_stage_application requires the validated Soui target platform")
  endif()
  find_program(SOUI_DESKTOP_NODE_EXECUTABLE NAMES node nodejs REQUIRED)
  set(tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../share/SouiDesktop/tools/stage-desktop-app.cjs")
  if(NOT EXISTS "${tool}")
    set(tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../tools/stage-desktop-app.cjs")
  endif()
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "SouiDesktop application staging tool is missing")
  endif()
  get_filename_component(assets "${APP_ASSETS}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(output "${APP_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  set(identifier)
  if(DEFINED APP_IDENTIFIER)
    list(APPEND identifier --identifier "${APP_IDENTIFIER}")
  endif()
  add_custom_target("${APP_TARGET}_stage"
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" "${tool}"
      --binary "$<TARGET_FILE:${APP_TARGET}>" --assets "${assets}" --output "${output}"
      --name "${APP_NAME}" --platform "${platform}" --arch "${architecture}"
      --version "${APP_VERSION}" ${identifier}
    DEPENDS "${APP_TARGET}" VERBATIM
    COMMENT "Stage unsigned application directory; system WebView dependencies remain external")
endfunction()

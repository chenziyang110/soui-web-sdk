include_guard(GLOBAL)

# Development resource output only. Never adopts an existing unmanaged web tree.
function(soui_sync_application_assets)
  cmake_parse_arguments(PARSE_ARGV 0 APP "" "TARGET;ASSETS" "")
  if(APP_UNPARSED_ARGUMENTS OR APP_KEYWORDS_MISSING_VALUES OR
      NOT APP_TARGET OR NOT APP_ASSETS)
    message(FATAL_ERROR "soui_sync_application_assets requires TARGET and ASSETS only")
  endif()
  if(NOT TARGET "${APP_TARGET}")
    message(FATAL_ERROR "soui_sync_application_assets TARGET must name an executable")
  endif()
  get_target_property(kind "${APP_TARGET}" TYPE)
  if(NOT kind STREQUAL "EXECUTABLE")
    message(FATAL_ERROR "soui_sync_application_assets TARGET must be an executable")
  endif()
  find_program(SOUI_DESKTOP_NODE_EXECUTABLE NAMES node nodejs REQUIRED)
  set(tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../share/SouiDesktop/tools/sync-desktop-assets.cjs")
  if(NOT EXISTS "${tool}")
    set(tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../tools/sync-desktop-assets.cjs")
  endif()
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "SouiDesktop managed asset synchronizer is missing")
  endif()
  get_filename_component(assets "${APP_ASSETS}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  add_custom_target("${APP_TARGET}_assets"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${APP_TARGET}>"
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" "${tool}"
      --input "${assets}" --output "$<TARGET_FILE_DIR:${APP_TARGET}>/web"
    VERBATIM COMMENT "Synchronize owned development assets; refuse unmanaged or modified output")
  add_dependencies("${APP_TARGET}" "${APP_TARGET}_assets")
endfunction()

# Creates an explicit, non-ALL staging target. Re-running it refuses an existing
# output directory; callers choose a new destination for each staged artifact.
function(soui_stage_application)
  cmake_parse_arguments(PARSE_ARGV 0 APP "USE_INSTALL_RPATH" "TARGET;NAME;ASSETS;OUTPUT;VERSION;IDENTIFIER;ARCHIVE_OUTPUT" "")
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
  if(APP_USE_INSTALL_RPATH)
    if(NOT platform STREQUAL "linux")
      message(FATAL_ERROR "USE_INSTALL_RPATH is supported only for Linux application targets")
    endif()
    get_target_property(imported "${APP_TARGET}" IMPORTED)
    get_target_property(alias "${APP_TARGET}" ALIASED_TARGET)
    if(imported OR alias)
      message(FATAL_ERROR "USE_INSTALL_RPATH requires a local non-alias executable target")
    endif()
    # The target itself (including development runs) uses the deployment policy.
    # Preserve explicit INSTALL_RPATH; never append development dependency paths.
    set_target_properties("${APP_TARGET}" PROPERTIES
      BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH_USE_LINK_PATH FALSE)
  endif()
  find_program(SOUI_DESKTOP_NODE_EXECUTABLE NAMES node nodejs REQUIRED)
  set(tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../share/SouiDesktop/tools/stage-desktop-app.cjs")
  if(NOT EXISTS "${tool}")
    set(tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../tools/stage-desktop-app.cjs")
  endif()
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "SouiDesktop application staging tool is missing")
  endif()
  get_filename_component(tool_directory "${tool}" DIRECTORY)
  set(verifier "${tool_directory}/verify-desktop-app.cjs")
  if(NOT EXISTS "${verifier}")
    message(FATAL_ERROR "SouiDesktop application verifier is missing")
  endif()
  set(archiver "${tool_directory}/archive-desktop-app.cjs")
  if(NOT EXISTS "${archiver}")
    message(FATAL_ERROR "SouiDesktop application archiver is missing")
  endif()
  get_filename_component(assets "${APP_ASSETS}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(output "${APP_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  if(DEFINED APP_ARCHIVE_OUTPUT)
    get_filename_component(archive_output "${APP_ARCHIVE_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  else()
    set(archive_output "${output}-delivery")
  endif()
  set(identifier)
  if(DEFINED APP_IDENTIFIER)
    list(APPEND identifier --identifier "${APP_IDENTIFIER}")
  endif()
  add_custom_target("${APP_TARGET}_stage"
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" "${tool}"
      --binary "$<TARGET_FILE:${APP_TARGET}>" --assets "${assets}" --output "${output}"
      --name "${APP_NAME}" --platform "${platform}" --arch "${architecture}"
      --version "${APP_VERSION}" ${identifier}
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" "${verifier}"
      --input "${output}" --platform "${platform}" --arch "${architecture}"
    DEPENDS "${APP_TARGET}" VERBATIM
    COMMENT "Stage unsigned application directory; system WebView dependencies remain external")
  # Intentionally independent: verification must not rebuild or overwrite a handoff.
  add_custom_target("${APP_TARGET}_verify_stage"
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" "${verifier}"
      --input "${output}" --platform "${platform}" --arch "${architecture}"
    VERBATIM COMMENT "Verify staged application integrity without executing it")
  add_custom_target("${APP_TARGET}_archive_stage"
    COMMAND "${SOUI_DESKTOP_NODE_EXECUTABLE}" "${archiver}"
      --input "${output}" --output "${archive_output}"
      --platform "${platform}" --arch "${architecture}"
    VERBATIM COMMENT "Archive verified staged application into a new delivery directory")
endfunction()

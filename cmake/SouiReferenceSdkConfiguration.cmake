# The installed SDK proof is for one configuration. Keep the native Multi-Config
# profile intact, but reject any consumer build that selects another archive.
if(CMAKE_SCRIPT_MODE_FILE)
  if(NOT SOUI_REFERENCE_VERIFIED_CONFIGURATION MATCHES "^(Debug|Release)$" OR
      NOT SOUI_REFERENCE_ACTUAL_CONFIGURATION STREQUAL SOUI_REFERENCE_VERIFIED_CONFIGURATION)
    message(FATAL_ERROR "Reference consumer configuration differs from the verified SDK configuration")
  endif()
  return()
endif()

function(soui_reference_sdk_configuration target verified_configuration)
  if(NOT verified_configuration MATCHES "^(Debug|Release)$" OR NOT TARGET "${target}")
    message(FATAL_ERROR "Reference requires an existing installed SDK target and exact Debug or Release configuration")
  endif()
  get_target_property(imported "${target}" IMPORTED)
  if(NOT imported)
    message(FATAL_ERROR "Reference SDK configuration must bind an imported target")
  endif()
  string(TOUPPER "${verified_configuration}" verified_upper)
  get_target_property(archive "${target}" "IMPORTED_LOCATION_${verified_upper}")
  if(NOT IS_ABSOLUTE "${archive}" OR NOT EXISTS "${archive}" OR IS_DIRECTORY "${archive}")
    message(FATAL_ERROR "Reference SDK has no actual archive for the verified configuration")
  endif()
  get_property(mapping_set TARGET "${target}" PROPERTY "MAP_IMPORTED_CONFIG_${verified_upper}" SET)
  if(mapping_set)
    get_target_property(mapping "${target}" "MAP_IMPORTED_CONFIG_${verified_upper}")
    string(TOUPPER "${mapping}" mapping_upper)
    if(NOT mapping_upper STREQUAL verified_upper)
      message(FATAL_ERROR "Reference imported SDK mapping differs from the verified configuration")
    endif()
  endif()
  # Disable CMake's imported-configuration fallback for the permitted build.
  set_property(TARGET "${target}" PROPERTY "MAP_IMPORTED_CONFIG_${verified_upper}" "${verified_upper}")
  if(TARGET soui_reference_sdk_configuration_check)
    get_property(existing_configuration GLOBAL PROPERTY SOUI_REFERENCE_BOUND_SDK_CONFIGURATION)
    if(NOT existing_configuration STREQUAL verified_configuration)
      message(FATAL_ERROR "Reference SDK targets use different verified configurations")
    endif()
  else()
    set_property(GLOBAL PROPERTY SOUI_REFERENCE_BOUND_SDK_CONFIGURATION "${verified_configuration}")
    add_custom_target(soui_reference_sdk_configuration_check
      COMMAND "${CMAKE_COMMAND}"
        "-DSOUI_REFERENCE_ACTUAL_CONFIGURATION=$<CONFIG>"
        "-DSOUI_REFERENCE_VERIFIED_CONFIGURATION=${verified_configuration}"
        -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
      VERBATIM)
  endif()
endfunction()

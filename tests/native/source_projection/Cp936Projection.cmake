function(soui_cp936_source_projection target raw_directory)
  if(SOUI_NATIVE_TOOLCHAIN STREQUAL "LLVM22MSVC142")
    # clang-cl accepts UTF-8, while these two legacy source leaves explicitly
    # compile CP936. Keep their raw source-bound copies and compile a separate
    # strict, round-trip-equivalent UTF-8 representation.
    set(projector "${SOUI_DISTRIBUTION_ROOT}/tests/native/source_projection/Project-Cp936.ps1")
    set(projected "${CMAKE_CURRENT_BINARY_DIR}/generated-utf8")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${projector}")
    execute_process(COMMAND "${SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE}" -NoProfile -NonInteractive
      -File "${projector}" -SourceRoot "${SOUI_DISTRIBUTION_ROOT}"
      -RawRoot "${raw_directory}" -OutputRoot "${projected}" COMMAND_ERROR_IS_FATAL ANY)
    target_include_directories(${target} PRIVATE "${projected}")
    target_compile_options(${target} PRIVATE /source-charset:utf-8 /execution-charset:utf-8)
  else()
    target_include_directories(${target} PRIVATE "${raw_directory}")
    target_compile_options(${target} PRIVATE /source-charset:.936 /execution-charset:utf-8)
  endif()
endfunction()

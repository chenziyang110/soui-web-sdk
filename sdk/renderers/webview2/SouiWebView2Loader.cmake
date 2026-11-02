function(soui_deploy_webview2_loader target)
  if(NOT TARGET "${target}" OR NOT TARGET Soui::WebView2)
    message(FATAL_ERROR "WebView2 loader deployment requires an application and Soui::WebView2")
  endif()
  get_target_property(loader Soui::WebView2 SOUI_WEBVIEW2_LOADER_RUNTIME)
  if(loader)
    if(NOT EXISTS "${loader}" OR IS_DIRECTORY "${loader}")
      message(FATAL_ERROR "Missing application-local WebView2 loader: ${loader}")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${loader}" "$<TARGET_FILE_DIR:${target}>/WebView2Loader.dll"
      VERBATIM)
  endif()
endfunction()

#pragma once
// Experimental host only. Bind the APIs actually used by this adapter, not
// every optional entry in a newer vendor header. Missing required APIs fail
// before initialization; no stub exports or weakened origin checks.
inline bool initialize_source_kernel(HMODULE module) {
#define BIND_SOURCE_API(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(module, #name)); if (!name) return false;
  BIND_SOURCE_API(jsArg); BIND_SOURCE_API(jsArgCount); BIND_SOURCE_API(jsBoolean);
  BIND_SOURCE_API(jsIsString); BIND_SOURCE_API(wkeGetURL);
  BIND_SOURCE_API(jsDouble); BIND_SOURCE_API(jsEval); BIND_SOURCE_API(jsGetWebView);
  BIND_SOURCE_API(jsString); BIND_SOURCE_API(jsToBoolean); BIND_SOURCE_API(jsToInt);
  BIND_SOURCE_API(jsToString); BIND_SOURCE_API(jsToTempString); BIND_SOURCE_API(jsUndefined);
  BIND_SOURCE_API(wkeCreateWebView); BIND_SOURCE_API(wkeDestroyWebView);
  BIND_SOURCE_API(wkeFinalize); BIND_SOURCE_API(wkeGetHostHWND); BIND_SOURCE_API(wkeGetString);
  BIND_SOURCE_API(wkeGlobalExec); BIND_SOURCE_API(wkeIsInitialize); BIND_SOURCE_API(wkeIsMainFrame);
  BIND_SOURCE_API(wkeIsProcessingUserGesture); BIND_SOURCE_API(wkeJsBindFunction); BIND_SOURCE_API(wkeLoadFileW);
  BIND_SOURCE_API(wkeOnDidCreateScriptContext); BIND_SOURCE_API(wkeOnDocumentReady);
  BIND_SOURCE_API(wkeOnNavigation); BIND_SOURCE_API(wkeOnPaintUpdated); BIND_SOURCE_API(wkeReload);
  BIND_SOURCE_API(wkeIsDirty); BIND_SOURCE_API(wkeRepaintIfNeeded); BIND_SOURCE_API(wkeResize); BIND_SOURCE_API(wkeResizeWindow);
  BIND_SOURCE_API(wkeRunJS); BIND_SOURCE_API(wkeSetContextMenuEnabled); BIND_SOURCE_API(wkeSetDebugConfig);
  BIND_SOURCE_API(wkeSetFocus); BIND_SOURCE_API(wkeSetNavigationToNewWindowEnable);
  BIND_SOURCE_API(wkeKillFocus); BIND_SOURCE_API(wkeSetHandle);
  BIND_SOURCE_API(wkeFireMouseEvent); BIND_SOURCE_API(wkeFireMouseWheelEvent);
  BIND_SOURCE_API(wkeFireKeyDownEvent); BIND_SOURCE_API(wkeFireKeyUpEvent);
  BIND_SOURCE_API(wkeFireKeyPressEvent); BIND_SOURCE_API(wkeSetComposition);
  BIND_SOURCE_API(wkeConfirmComposition); BIND_SOURCE_API(wkeCancelComposition);
  BIND_SOURCE_API(wkeGetCaretRect); BIND_SOURCE_API(wkeGetInteractionApi);
  BIND_SOURCE_API(wkeSetZoomFactor); BIND_SOURCE_API(wkeShowWindow); BIND_SOURCE_API(wkeShutdown);
  BIND_SOURCE_API(wkeUpdate); BIND_SOURCE_API(wkeWake);
#undef BIND_SOURCE_API
  wkeOnDocumentReady2 = reinterpret_cast<FN_wkeOnDocumentReady2>(
      GetProcAddress(module, "wkeOnDocumentReady2")); // required when the controlled broker is enabled
  wkeGetCursorBrokerApi = reinterpret_cast<FN_wkeGetCursorBrokerApi>(
      GetProcAddress(module, "wkeGetCursorBrokerApi")); // optional, versioned capability
  const auto initialize = reinterpret_cast<FN_wkeInitializeEx>(GetProcAddress(module, "wkeInitializeEx"));
  if (!initialize) return false;
  initialize(nullptr);
  return wkeIsInitialize() != 0;
}

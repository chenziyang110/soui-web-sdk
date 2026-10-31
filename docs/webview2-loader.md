# Optional WebView2 loader for AutoHost

The WebView2 SDK ships both a static loader library and a shared `WebView2Loader.dll`. The static library can carry OS imports that fail process startup on older Windows versions before `Soui::AutoHost` can select MiniBlink.

`SOUI_WEBVIEW2_LOADER_SHARED` is an opt-in CMake switch. When enabled, the SDK keeps the official loader DLL and import library as authenticated package inputs, but does not link the import library into `soui_webview2`. The host dynamically loads the DLL beside the executable and resolves the two loader entry points it needs. Missing files, missing exports, or an unavailable Runtime are returned as ordinary WebView2 discovery failures, so AutoHost can continue to its configured MiniBlink fallback.

The option is off by default, preserving existing static-loader consumers. Applications enabling it must deploy the architecture-matched `WebView2Loader.dll` beside each WebView2/AutoHost executable. The SDK does not install or download a WebView2 Runtime.

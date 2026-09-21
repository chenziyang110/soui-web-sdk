#include "wke/wkeGlobalVar.h"
#include "third_party/libcurl/include/curl/curl.h"
#include <cstdarg>

namespace wke {

bool g_isSetDragEnable = true;
bool g_isSetDragDropEnable = true;

std::string* s_versionString = nullptr;
bool wkeIsInit = false;

unsigned long g_kWakeMinInterval = 5;
double g_kDrawMinInterval = 0.003;
bool g_isDecodeUrlRequest = false;
void* g_tipPaintCallback = nullptr;
float g_contentScale = 1;
bool g_rendererAntiAlias = false;
bool g_diskCacheEnable = false;
std::unique_ptr<std::string> g_DNS;
wkeOnNetCurlSetoptCallback g_curlSetoptCallback = nullptr;
extern "C" CURLcode Curl_vsetopt(CURL* data, CURLoption option, va_list args);
void WKE_CALL_TYPE curlSetopt(void* curlHandle, __int32 option, ...) {
    static_assert(sizeof(CURLoption) == sizeof(__int32), "Curl option ABI mismatch");
    va_list args;
    va_start(args, option);
    Curl_vsetopt(curlHandle, static_cast<CURLoption>(option), args);
    va_end(args);
}
bool g_smootTextEnable = false;
bool g_consoleOutputEnable = true;

wkeUiThreadPostTaskCallback g_wkeUiThreadPostTaskCallback = nullptr;

wkeWillMediaLoadCallback g_wkeWillMediaLoadCallback = nullptr;
void* g_wkeWillMediaLoadCallbackCallbackParam = nullptr;

wkeOnPluginFindCallback g_wkePluginFindcallback = nullptr;
void* g_wkePluginFindcallbackParam = nullptr;

wkeMediaPlayerFactory g_wkeMediaPlayerFactory = nullptr;
wkeOnIsMediaPlayerSupportsMIMEType g_onIsMediaPlayerSupportsMIMETypeCallback = nullptr;

wkeTempCallbackInfo g_wkeTempCallbackInfo;

std::set<wkeWebView> g_liveWebViews;

DWORD g_contextMenuItemMask = kWkeMenuSelectedAllId | kWkeMenuSelectedTextId | kWkeMenuUndoId | kWkeMenuCopyImageId | kWkeMenuInspectElementAtId |
    kWkeMenuCutId | kWkeMenuPasteId;
}

WKE_FILE_OPEN g_pfnOpen;
WKE_FILE_CLOSE g_pfnClose;
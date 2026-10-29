#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wrl.h>
#include <json.hpp>
#include <soui/version.h>
#include "loader_policy.h"
#include "preload.h"
#include "diagnostic-webview2-session.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
using json = nlohmann::json;
constexpr wchar_t entry[] = L"https://soui-webview2.invalid/index.html";
constexpr wchar_t origin[] = L"https://soui-webview2.invalid/";
void check(HRESULT result, const char* operation) {
  if(FAILED(result)) throw std::runtime_error(operation);
}
struct text { LPWSTR value{}; ~text(){CoTaskMemFree(value);} };
std::string utf8(const std::wstring& value) {
  if(value.size()>1024U*1024U) throw std::runtime_error("UTF-16 limit");
  if(value.empty())return {};
  const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
  if(!count)throw std::runtime_error("Invalid UTF-16");
  std::string out(static_cast<size_t>(count),'\0');
  if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count,nullptr,nullptr))
    throw std::runtime_error("UTF-16 conversion failed");
  return out;
}
std::wstring wide(const std::string& value) {
  if(value.size()>2U*1024U*1024U)throw std::runtime_error("UTF-8 limit");
  if(value.empty())return {};
  const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
  if(!count)throw std::runtime_error("Invalid UTF-8");
  std::wstring out(static_cast<size_t>(count),L'\0');
  if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count))
    throw std::runtime_error("UTF-8 conversion failed");
  return out;
}
std::string guid() {
  GUID value{};check(CoCreateGuid(&value),"Create identity");wchar_t raw[40]{};
  if(!StringFromGUID2(value,raw,40))throw std::runtime_error("Identity conversion");
  return utf8(raw);
}
std::string fileHash(const std::filesystem::path& file) {
  struct handles {
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    std::vector<UCHAR> storage;
    ~handles(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
  } state;
  if(BCryptOpenAlgorithmProvider(&state.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA provider");
  ULONG size{},written{};
  if(BCryptGetProperty(state.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&written,0)<0 || written!=sizeof(size) || !size || size>1024U*1024U)throw std::runtime_error("SHA storage");
  state.storage.resize(size);
  if(BCryptCreateHash(state.algorithm,&state.hash,state.storage.data(),size,nullptr,0,0)<0)throw std::runtime_error("SHA initialization");
  std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Identity file read");
  char chunk[65536];while(input){input.read(chunk,sizeof(chunk));const auto bytes=input.gcount();if(bytes>0 && BCryptHashData(state.hash,reinterpret_cast<PUCHAR>(chunk),static_cast<ULONG>(bytes),0)<0)throw std::runtime_error("SHA update");}
  if(input.bad())throw std::runtime_error("Identity file read failed");
  UCHAR digest[32]{};if(BCryptFinishHash(state.hash,digest,sizeof(digest),0)<0)throw std::runtime_error("SHA finish");
  const char hex[]="0123456789abcdef";std::string result;result.reserve(64);for(const auto value:digest){result+=hex[value>>4];result+=hex[value&15];}return result;
}
bool trustedEntry(std::wstring uri) {
  uri.resize(uri.find(L'#')==std::wstring::npos ? uri.size() : uri.find(L'#'));
  return uri==entry;
}
bool trustedResource(const std::wstring& uri) {return uri.rfind(origin,0)==0 && uri.find(L'\\')==std::wstring::npos;}
bool within(const std::filesystem::path& child,const std::filesystem::path& parent) {
  auto prefix=parent.native();if(prefix.back()!=L'\\')prefix+=L'\\';
  const auto candidate=child.native();
  return CompareStringOrdinal(candidate.c_str(),-1,parent.c_str(),-1,TRUE)==CSTR_EQUAL ||
    (candidate.size()>=prefix.size() && CompareStringOrdinal(candidate.data(),static_cast<int>(prefix.size()),prefix.data(),static_cast<int>(prefix.size()),TRUE)==CSTR_EQUAL);
}
json inventoryResources(const std::filesystem::path& root) {
  if(!std::filesystem::is_regular_file(root/L"index.html"))throw std::runtime_error("Missing Scratch index.html");
  unsigned files=0;uint64_t total=0;std::vector<std::filesystem::path> paths;
  for(const auto& item:std::filesystem::recursive_directory_iterator(root)) {
    const auto attributes=GetFileAttributesW(item.path().c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Resource links/reparse points are not admitted");
    if(item.is_directory())continue;
    if(!item.is_regular_file())throw std::runtime_error("Non-regular resource");
    const auto size=item.file_size();
    if(++files>4096 || size>32U*1024U*1024U || (total+=size)>256U*1024U*1024U)
      throw std::runtime_error("Scratch resource budget exceeded");
    paths.push_back(item.path());
  }
  std::sort(paths.begin(),paths.end());
  json inventory{{"schema","soui.scratch-resource-inventory/v1"},{"files",json::array()},
    {"fileCount",files}};
  total=0;
  for(const auto& path:paths) {
    const auto size=std::filesystem::file_size(path);
    const auto modified=std::filesystem::last_write_time(path);
    if(size>32U*1024U*1024U || (total+=size)>256U*1024U*1024U)
      throw std::runtime_error("Scratch resource budget changed before identity capture");
    const auto digest=fileHash(path);
    if(std::filesystem::file_size(path)!=size || std::filesystem::last_write_time(path)!=modified)
      throw std::runtime_error("Resource changed while recording identity");
    inventory["files"].push_back({{"path",utf8(path.lexically_relative(root).generic_wstring())},
      {"bytes",size},{"sha256",digest}});
  }
  inventory["totalBytes"]=total;
  return inventory;
}
struct application : std::enable_shared_from_this<application> {
  HWND window{};
  std::filesystem::path resources,output,profile;
  int width{1424},height{861};
  bool closing{},navigationAllowed{true},ready{},active{},sampleTaken{},controllerClosed{},controllerCloseActive{},outputOwned{};
  unsigned preparing{},scriptCallbacks{},activeCallbacks{},blockedResources{};int exitCode{};ULONGLONG closeAt{},measureAt{};
  HANDLE browser{};UINT32 browserPid{};
  std::string documentId{guid()},sessionId{guid()},token{guid()},sampleId;
  ComPtr<ICoreWebView2Environment> environment;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> core;
  bool diagnosticMode{},diagnosticPublished{};
  std::shared_ptr<soui::scratch_diagnostic::webview2_capture> diagnostic;
  json failures=json::array(),windowEvents=json::array();
  json record{{"schema","soui.scratch-raw-webview2-session/v1"},{"backend","W: raw WebView2 HWND"},
    {"productionSdk",false},{"performanceVerified",false},{"pass",false}};
  ~application(){if(controller)controller->Close();if(window && IsWindow(window))DestroyWindow(window);if(browser)CloseHandle(browser);}
  void save(){if(!outputOwned)throw std::runtime_error("No owned report directory");std::ofstream stream(output/L"session.json",std::ios::binary|std::ios::trunc);stream<<record.dump(2);if(!stream)throw std::runtime_error("Report write failed");}
  void failed(const std::string& reason) noexcept {
    exitCode=2;
    try{if(failures.size()<64)failures.push_back(reason.substr(0,1024));else record["failureOverflow"]=true;record["failures"]=failures;record["pass"]=false;save();}catch(...){}
    if(window)PostMessageW(window,WM_CLOSE,0,0);
  }
  template<class Work> HRESULT boundary(Work work) noexcept {
    struct callback_scope {
      unsigned& active;
      explicit callback_scope(unsigned& count) noexcept : active(count){++active;}
      ~callback_scope(){--active;}
    } current(activeCallbacks);
    try{work();}catch(const std::exception& error){failed(error.what());}catch(...){failed("Unknown callback failure");}
    return S_OK;
  }
  void bounds() {
    const auto current=controller;
    if(!current)return;RECT rect{};if(!GetClientRect(window,&rect))throw std::runtime_error("Client bounds");
    check(current->put_Bounds(rect),"Set bounds");
  }
  void resizeEvent(const char* kind) {
    if(!active)return;
    if(windowEvents.size()>=256){failed("Window event inventory overflow");return;}
    windowEvents.push_back({{"kind",kind},{"tickMs",std::to_string(GetTickCount64())}});
  }
  void close() {
    if(!closing){closing=true;closeAt=GetTickCount64();if(diagnostic)diagnostic->cancel();if(active)failed("Closed during sample");}
    if(diagnostic)diagnostic->cancel();
    record["nativeCallbacksPending"]={{"initialization",preparing},{"script",scriptCallbacks},{"active",activeCallbacks},
      {"diagnostic",diagnostic?diagnostic->pending():0},{"diagnosticActive",diagnostic?diagnostic->active_callbacks():0}};
    if(!SetTimer(window,1,20,nullptr)){
      try{record["cleanup"]="close-watchdog-unavailable-not-passed";record["pass"]=false;save();}catch(...){}
      ExitProcess(3);
    }
    if(preparing || activeCallbacks || controllerCloseActive || (diagnostic && diagnostic->active_callbacks()))return;
    if(controller && !controllerClosed){
      const auto current=controller;
      // Close may synchronously reenter the owner message pump. Publish the
      // claim first, retain the COM instance and let callbacks unwind before
      // releasing its ownership below.
      struct close_scope {
        bool& active;
        explicit close_scope(bool& flag) noexcept : active(flag){active=true;}
        ~close_scope(){active=false;}
      } closingController(controllerCloseActive);
      controllerClosed=true;
      const auto result=current->Close();
      if(FAILED(result)){controllerClosed=false;check(result,"Close controller");}
      core.Reset();controller.Reset();
    }
    // Close can release a paused/blocked ExecuteScript; keep its native
    // completion owner alive until the real callback arrives.
    if(scriptCallbacks || (diagnostic && (diagnostic->pending() || diagnostic->active_callbacks() || !diagnostic->events_retired())))return;
    if(browser && WaitForSingleObject(browser,0)!=WAIT_OBJECT_0)return;
    environment.Reset();
    // This original 10s deadline covers native target/callback retirement. Large
    // evidence exports begin only after the actual closure proof, as pure I/O.
    const auto nativeCloseElapsed=GetTickCount64()-closeAt;
    if(nativeCloseElapsed>=10000){record["cleanup"]="deadline-failed-before-export";record["pass"]=false;save();ExitProcess(3);}
    record["nativeRetiredBeforeExport"]=browser?json(true):json(nullptr);record["nativeCloseElapsedMs"]=nativeCloseElapsed;
    KillTimer(window,1);
    if(diagnostic)diagnostic->export_cancelled_after_retirement();
    record["cleanup"]=browser ? "owned-browser-signaled" : "no-browser-identity";
    record["failures"]=failures;record["windowEvents"]=windowEvents;record["blockedResources"]=blockedResources;
    record["pass"]=sampleTaken && exitCode==0 && failures.empty() && windowEvents.empty() && blockedResources==0;save();
    DestroyWindow(window);
  }
  void sample() {
    if(diagnosticMode)return;
    if(!ready || active || closing || !core || record.value("samples",0)>=3 || IsIconic(window) || GetForegroundWindow()!=window)return;
    if(blockedResources){failed("Blocked external requests invalidate this comparison cohort");return;}
    active=true;sampleId=guid();measureAt=GetTickCount64();windowEvents=json::array();
    const std::string script="(async function(){const p=window.souiScratchPerformance;try{if(!p||!p.configuration||document.visibilityState!=='visible')throw Error('Load a scene first');const cfg=p.configuration;await new Promise(r=>setTimeout(r,10000));if(p.configuration!==cfg||document.visibilityState!=='visible'||innerWidth!=="+std::to_string(width)+"||innerHeight!=="+std::to_string(height)+"||devicePixelRatio!==1)throw Error('Viewport/DPR/scene changed');p.meter.start({...cfg,durationMs:30000});await new Promise(r=>setTimeout(r,30300));chrome.webview.postMessage(JSON.stringify({kind:'baseline.report',documentId:"+json(documentId).dump()+",sampleId:"+json(sampleId).dump()+",report:p.meter.collectReport()}));}catch(e){chrome.webview.postMessage(JSON.stringify({kind:'baseline.error',documentId:"+json(documentId).dump()+",sampleId:"+json(sampleId).dump()+",error:String(e)}));}})()";
    const auto weak=weak_from_this();
    ++scriptCallbacks;
    const auto accepted=core->ExecuteScript(wide(script).c_str(),Callback<ICoreWebView2ExecuteScriptCompletedHandler>([weak](HRESULT result,LPCWSTR)->HRESULT{
      const auto self=weak.lock();if(self){--self->scriptCallbacks;if(FAILED(result))self->failed("Sample script rejected");}return S_OK;}).Get());
    if(FAILED(accepted)){--scriptCallbacks;check(accepted,"Start sample script");}
    if(closing)return;
    if(!SetTimer(window,2,45000,nullptr))throw std::runtime_error("Measurement watchdog unavailable");
  }
  void receive(ICoreWebView2WebMessageReceivedEventArgs* args) {
    const auto currentCore=core;
    const auto current=[&]{return !closing && currentCore && core.Get()==currentCore.Get();};
    if(!current())return;
    text source,raw;check(args->get_Source(&source.value),"Message source");
    if(!current() || !source.value || !trustedEntry(source.value))return;
    text currentSource;check(currentCore->get_Source(&currentSource.value),"Current source");
    if(!current() || !currentSource.value || !trustedEntry(currentSource.value))return;
    if(FAILED(args->TryGetWebMessageAsString(&raw.value)) || !raw.value || wcsnlen_s(raw.value,2U*1024U*1024U+1)>2U*1024U*1024U)return;
    if(!current())return;
    const auto bytes=utf8(raw.value);
    if(bytes.size()>2U*1024U*1024U)return;
    const auto packet=json::parse(bytes,[](int depth,json::parse_event_t,json&){if(depth>40)throw std::runtime_error("Message depth exceeded");return true;});
    if(!packet.is_object() || packet.value("documentId",std::string{})!=documentId)return;
    const auto kind=packet.value("kind",std::string{});
    if(kind=="baseline.report" || kind=="baseline.error") {
      if(!active || packet.value("sampleId",std::string{})!=sampleId)return;
      active=false;KillTimer(window,2);
      if(kind=="baseline.error"){failed(packet.value("error",std::string{"Sample failed"}));return;}
      const auto& report=packet.at("report");
      // Slow, censored and zero-rAF windows are legitimate bad measurements.
      // Only the separate full-window gate governs collection admission; the
      // closed-interval percentile gate remains diagnostic in the raw report.
      const bool pageValid=report.is_object() && report.value("measurementVersion",0)==2 &&
        report.value("validForWindowRateComparison",false) && report.contains("windowRafFps") &&
        report["windowRafFps"].is_number() && std::isfinite(report["windowRafFps"].get<double>()) &&
        report["windowRafFps"].get<double>()>=0;
      if(!pageValid || !windowEvents.empty() || blockedResources)exitCode=2;
      const auto destination=output/(L"webview2-run-"+std::to_wstring(record.value("samples",0)+1)+L".json");
      std::ofstream stream(destination,std::ios::binary);stream<<report.dump(2);if(!stream)throw std::runtime_error("Sample output failed");
      record["samples"]=record.value("samples",0)+1;record["lastSample"]=utf8(destination.filename().native());
      record["runs"].push_back({{"file",record["lastSample"]},{"pageValidForWindowRate",pageValid},
        {"percentilesQualified",report.is_object() && report.value("validForComparison",false)},
        {"windowEvents",windowEvents}});
      record["windowEvents"]=windowEvents;record["blockedResources"]=blockedResources;sampleTaken=true;record["pass"]=exitCode==0 && failures.empty();save();
      SetWindowTextW(window,exitCode ? L"Raw WebView2 W — sample invalid" : L"Raw WebView2 W — sample saved (F6 repeats)");return;
    }
    if(bytes.size()>65536 || !packet.contains("request") || !packet["request"].is_string())return;
    const std::string requestText=packet["request"].get<std::string>();if(requestText.size()>65536)return;
    const auto request=json::parse(requestText,[](int depth,json::parse_event_t,json&){if(depth>32)throw std::runtime_error("Request depth exceeded");return true;});
    const auto id=request.value("requestId",std::string{}),invocation=packet.value("invocationId",std::string{});
    if(id.empty() || id.size()>512 || invocation.empty() || invocation.size()>32)return;
    json response{{"requestId",id},{"ok",true}};
    const auto method=request.value("method",std::string{});
    if(method=="bridge.bootstrap")response["result"]={{"sessionId",sessionId},{"origin","app://scratch-studio"},{"token",token}};
    else if(request.value("sessionId",std::string{})!=sessionId || request.value("token",std::string{})!=token || request.value("origin",std::string{})!="app://scratch-studio")
      response={{"requestId",id},{"ok",false},{"error",{{"code","E_SESSION"},{"message","Invalid comparison session"}}}};
    else if(method=="app.info")response["result"]={{"sdkVersion",SOUI_SDK_VERSION_STRING},{"buildId","raw WebView2 comparison W"},{"backend","WebView2 raw W"},{"runtimeVersion",record.value("runtimeVersion",std::string{})}};
    else if(method=="bridge.probe.report"){
      ready=true;KillTimer(window,3);response["result"]={{"ready",true}};
      if(diagnosticMode && !diagnostic && !SetTimer(window,4,500,nullptr))throw std::runtime_error("Diagnostic owner timer unavailable");
    }
    else response={{"requestId",id},{"ok",false},{"error",{{"code","E_UNSUPPORTED"},{"message","Rendering comparison only; native business actions are unavailable"}}}};
    const auto envelope=json{{"kind","response"},{"documentId",documentId},{"requestId",id},{"invocationId",invocation},{"raw",response.dump()}}.dump();
    const auto posted=currentCore->PostWebMessageAsJson(wide(envelope).c_str());
    if(current())check(posted,"Send comparison response");
  }
  void install() {
    ComPtr<ICoreWebView2Settings> settings;check(core->get_Settings(&settings),"Settings");
    check(settings->put_AreHostObjectsAllowed(FALSE),"Host objects");check(settings->put_AreDefaultScriptDialogsEnabled(FALSE),"Dialogs");
    check(settings->put_AreDevToolsEnabled(FALSE),"DevTools");check(settings->put_AreDefaultContextMenusEnabled(FALSE),"Context menu");
    check(settings->put_IsWebMessageEnabled(TRUE),"Messages");
    const auto weak=weak_from_this();EventRegistrationToken ignored{};
    check(controller->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>([weak](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* args)->HRESULT{
      const auto s=weak.lock();if(!s)return S_OK;return s->boundary([&]{UINT key{};COREWEBVIEW2_KEY_EVENT_KIND kind{};check(args->get_VirtualKey(&key),"Accelerator key");check(args->get_KeyEventKind(&kind),"Accelerator kind");if(key==VK_F6 && (kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)){check(args->put_Handled(TRUE),"Handle accelerator");PostMessageW(s->window,WM_APP+41,0,0);}});}).Get(),&ignored),"Sample accelerator");
    check(core->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
      const auto s=weak.lock();if(!s)return S_OK;return s->boundary([&]{text uri;check(args->get_Uri(&uri.value),"Navigation URI");const bool allow=!s->closing&&s->navigationAllowed&&uri.value&&trustedEntry(uri.value);check(args->put_Cancel(allow?FALSE:TRUE),"Navigation admission");if(allow)s->navigationAllowed=false;});}).Get(),&ignored),"Navigation policy");
    check(core->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT{
      const auto s=weak.lock();if(!s||s->closing)return S_OK;return s->boundary([&]{BOOL success{};check(args->get_IsSuccess(&success),"Navigation result");if(!success)throw std::runtime_error("Trusted navigation failed");});}).Get(),&ignored),"Navigation completion");
    check(core->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
      const auto s=weak.lock();if(!s)return S_OK;return s->boundary([&]{text uri;check(args->get_Uri(&uri.value),"Frame URI");const bool allow=!s->closing&&uri.value&&(std::wstring(uri.value)==L"about:blank"||std::wstring(uri.value)==L"about:srcdoc");check(args->put_Cancel(allow?FALSE:TRUE),"Frame admission");});}).Get(),&ignored),"Frame policy");
    check(core->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{return args->put_Handled(TRUE);}).Get(),&ignored),"Popup policy");
    check(core->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT{return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);}).Get(),&ignored),"Permission policy");
    ComPtr<ICoreWebView2_4> downloads;check(core.As(&downloads),"Downloads interface");
    check(downloads->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* args)->HRESULT{return args->put_Cancel(TRUE);}).Get(),&ignored),"Download policy");
    check(core->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*)->HRESULT{if(const auto s=weak.lock())s->failed("Renderer process failed");return S_OK;}).Get(),&ignored),"Process failure policy");
    ComPtr<ICoreWebView2_22> allSources;check(core.As(&allSources),"All-source resource interface");
    check(allSources->AddWebResourceRequestedFilterWithRequestSourceKinds(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL),"All-source filter");
    check(core->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([weak](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* args)->HRESULT{
      const auto s=weak.lock();if(!s)return S_OK;return s->boundary([&]{ComPtr<ICoreWebView2WebResourceRequest> req;text uri;check(args->get_Request(&req),"Request");check(req->get_Uri(&uri.value),"Request URI");if(uri.value&&trustedResource(uri.value))return;++s->blockedResources;ComPtr<ICoreWebView2WebResourceResponse> denied;check(s->environment->CreateWebResourceResponse(nullptr,403,L"Forbidden",L"",&denied),"Denial response");check(args->put_Response(denied.Get()),"Resource denial");});}).Get(),&ignored),"Resource policy");
    check(core->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([weak](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{const auto s=weak.lock();if(s)return s->boundary([&]{s->receive(args);});return S_OK;}).Get(),&ignored),"Message policy");
    ComPtr<ICoreWebView2_3> mapping;check(core.As(&mapping),"Virtual host interface");
    check(mapping->SetVirtualHostNameToFolderMapping(L"soui-webview2.invalid",resources.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY),"Resource mapping");
    std::string preload=baseline_preload;const auto at=preload.find("__SOUI_DOCUMENT_ID__");
    if(at==std::string::npos || preload.find("__SOUI_DOCUMENT_ID__",at+1)!=std::string::npos)throw std::runtime_error("Preload identity placeholder");
    preload.replace(at,sizeof("__SOUI_DOCUMENT_ID__")-1,json(documentId).dump());
    ++preparing;
    const auto admission=core->AddScriptToExecuteOnDocumentCreated(wide(preload).c_str(),Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>([weak](HRESULT result,LPCWSTR)->HRESULT{
      const auto s=weak.lock();if(!s)return S_OK;--s->preparing;return s->boundary([&]{check(result,"Preload registration");if(!s->closing)check(s->core->Navigate(entry),"Navigate Scratch");});}).Get());
    if(FAILED(admission)){--preparing;check(admission,"Preload admission");}
    bounds();if(closing)return;
    const auto initialController=controller;check(initialController->put_IsVisible(TRUE),"Show controller");
    if(closing)return;
    if(!SetTimer(window,3,90000,nullptr))throw std::runtime_error("Startup watchdog unavailable");
  }
  void start() {
    const auto admitted=soui::sdk::detail::check_webview2_loader_policy();if(!admitted.ok)throw std::runtime_error(admitted.code);
    auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();if(!options)throw std::runtime_error("Environment options");
    check(options->put_ExclusiveUserDataFolderAccess(TRUE),"Exclusive profile");BOOL exclusive{};check(options->get_ExclusiveUserDataFolderAccess(&exclusive),"Exclusive profile readback");if(!exclusive)throw std::runtime_error("Exclusive profile not active");
    const auto weak=weak_from_this();++preparing;
    const auto admittedEnvironment=CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),options.Get(),Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([weak](HRESULT result,ICoreWebView2Environment* env)->HRESULT{
      const auto s=weak.lock();if(!s)return S_OK;--s->preparing;return s->boundary([&]{check(result,"Environment creation");if(!env)throw std::runtime_error("Null environment");s->environment=env;text version;check(env->get_BrowserVersionString(&version.value),"Runtime identity");s->record["runtimeVersion"]=utf8(version.value?version.value:L"");s->save();if(s->closing){s->exitCode=2;return;}++s->preparing;
        const auto accepted=env->CreateCoreWebView2Controller(s->window,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([weak](HRESULT created,ICoreWebView2Controller* controller)->HRESULT{
          const auto current=weak.lock();if(!current)return S_OK;--current->preparing;return current->boundary([&]{check(created,"Controller creation");if(!controller)throw std::runtime_error("Null controller");current->controller=controller;check(controller->get_CoreWebView2(&current->core),"CoreWebView2");check(current->core->get_BrowserProcessId(&current->browserPid),"Browser PID");current->browser=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,current->browserPid);if(!current->browser)throw std::runtime_error("Browser identity handle");FILETIME birth{},exit{},kernel{},user{};if(!GetProcessTimes(current->browser,&birth,&exit,&kernel,&user))throw std::runtime_error("Browser creation identity");current->record["browserPid"]=current->browserPid;current->record["browserCreationFileTime"]=std::to_string((static_cast<uint64_t>(birth.dwHighDateTime)<<32)|birth.dwLowDateTime);if(!current->closing)current->install();});}).Get());
        if(FAILED(accepted)){--s->preparing;check(accepted,"Controller admission");}});}).Get());
    if(FAILED(admittedEnvironment)){--preparing;check(admittedEnvironment,"Environment admission");}
  }
  void diagnosticTick() {
    if(!diagnosticMode || !ready || closing)return;
    if(!diagnostic) {
      const auto weak=weak_from_this();
      diagnostic=std::make_shared<soui::scratch_diagnostic::webview2_capture>(core.Get(),output/L"diagnostic",[weak]{
        const auto s=weak.lock();if(!s)return json::object();RECT bounds{};const bool geometry=GetClientRect(s->window,&bounds)!=FALSE;
        return json{{"focused",GetForegroundWindow()==s->window},{"visible",IsWindowVisible(s->window)!=FALSE},
          {"minimized",IsIconic(s->window)!=FALSE},{"geometryQualified",geometry&&bounds.right==s->width&&bounds.bottom==s->height},
          {"width",bounds.right},{"height",bounds.bottom}};
      },width,height);
      active=true;measureAt=GetTickCount64();windowEvents=json::array();diagnostic->start();
    } else diagnostic->tick();
    if(diagnostic->done() && !diagnosticPublished) {
      diagnosticPublished=true;active=false;KillTimer(window,4);const auto& captured=diagnostic->result();
      record["diagnostic"]=captured;record["formalPerformancePassed"]=nullptr;
      sampleTaken=captured.value("collectionCompleted",false);record["windowEvents"]=windowEvents;
      record["pass"]=sampleTaken && captured.value("pageGuardsQualified",false) && windowEvents.empty() && failures.empty() && !blockedResources;
      if(!record["pass"].get<bool>())exitCode=2;save();
      SetWindowTextW(window,L"Raw WebView2 W — diagnostic saved (instrumented; not a score)");
      if(diagnostic->timed_out())failed("Diagnostic timeout requires owned Controller shutdown");
      else if(!PostMessageW(window,WM_CLOSE,0,0))failed("Diagnostic normal close request failed");
    }
  }
  static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto self=reinterpret_cast<application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->window=window;}
    if(!self)return DefWindowProcW(window,message,wparam,lparam);
    try {
      if(message==WM_SIZE){self->resizeEvent(wparam==SIZE_MINIMIZED?"minimized":"resize");self->bounds();return 0;}
      if(message==WM_ACTIVATE){if(LOWORD(wparam)==WA_INACTIVE)self->resizeEvent("blur");return 0;}
      if(message==WM_KEYDOWN && wparam==VK_F6){self->boundary([&]{self->sample();});return 0;}
      if(message==WM_APP+41){self->boundary([&]{self->sample();});return 0;}
      if(message==WM_SETFOCUS){const auto controller=self->controller;if(controller)check(controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC),"Focus");return 0;}
      if(message==WM_CLOSE){self->close();return 0;}
      if(message==WM_TIMER && wparam==1){if(GetTickCount64()-self->closeAt>10000){try{self->record["cleanup"]="deadline-failed-not-passed";self->record["pass"]=false;self->save();}catch(...){}ExitProcess(3);}self->close();return 0;}
      if(message==WM_TIMER && wparam==2){KillTimer(window,2);if(self->active)self->failed("Sample deadline exceeded");return 0;}
      if(message==WM_TIMER && wparam==3){KillTimer(window,3);if(!self->ready)self->failed("Scratch startup deadline exceeded");return 0;}
      if(message==WM_TIMER && wparam==4){self->boundary([&]{self->diagnosticTick();});return 0;}
      if(message==WM_DESTROY){PostQuitMessage(self->exitCode);return 0;}
      if(message==WM_NCDESTROY){SetWindowLongPtrW(window,GWLP_USERDATA,0);self->window=nullptr;}
    }catch(const std::exception& error){self->failed(error.what());return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
  }
};
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
  const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized))return 2;
  int result=2;
  std::shared_ptr<application> app;
  try {
    app=std::make_shared<application>();int argc{};wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("Arguments");
    struct arguments {wchar_t** value;~arguments(){LocalFree(value);}} owned{argv};
    for(int i=1;i<argc;i+=2){if(i+1>=argc)throw std::runtime_error("Option requires value");const std::wstring name=argv[i],value=argv[i+1];
      if(name==L"--resources")app->resources=std::filesystem::canonical(value);
      else if(name==L"--output")app->output=std::filesystem::absolute(value).lexically_normal();
      else if(name==L"--diagnostic"){if(value!=L"profile")throw std::runtime_error("Only --diagnostic profile is supported");app->diagnosticMode=true;}
      else if(name==L"--width"||name==L"--height"){if(value.empty()||value.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::runtime_error("Invalid dimensions");const auto size=std::stoul(value);if(size<200||size>8192)throw std::runtime_error("Dimension range");if(name==L"--width")app->width=static_cast<int>(size);else app->height=static_cast<int>(size);}
      else throw std::runtime_error("Unknown option");}
    if(app->resources.empty()||app->output.empty()||app->output.filename().empty())throw std::runtime_error("Required: --resources DIR --output NEW_DIRECTORY [--width 1424 --height 861]");
    // Resolve an existing parent junction before admitting a new child: a
    // lexical path outside resources can otherwise write into the served tree.
    app->output=std::filesystem::canonical(app->output.parent_path())/app->output.filename();
    if(within(app->output,app->resources)||within(app->resources,app->output))throw std::runtime_error("Output/profile and served resources must be disjoint");
    const auto resourceInventory=inventoryResources(app->resources);
    if(!std::filesystem::create_directory(app->output))throw std::runtime_error("Output exists; preserve earlier evidence");
    app->outputOwned=true;
    app->output=std::filesystem::canonical(app->output);
    if(within(app->output,app->resources)||within(app->resources,app->output))throw std::runtime_error("Created output overlaps the served resources");
    app->profile=app->output/L"profile";if(!std::filesystem::create_directory(app->profile))throw std::runtime_error("Profile must be new");
    app->profile=std::filesystem::canonical(app->profile);
    if(!within(app->profile,app->output)||within(app->profile,app->resources))throw std::runtime_error("Created profile escaped its output directory");
    app->record["resources"]=utf8(app->resources.native());app->record["requestedViewport"]={{"width",app->width},{"height",app->height},{"devicePixelRatio",1}};app->record["failures"]=json::array();app->record["runs"]=json::array();
    app->record["hostPid"]=GetCurrentProcessId();app->record["msvcVersion"]=_MSC_VER;
#ifdef _WIN64
    app->record["architecture"]="x64";
#else
    app->record["architecture"]="x86";
#endif
    for(const auto name:{"index.html","app.js","app.css","performance-scenes.js","performance-meter.js","scratch-gui-standalone.js"}){
      const auto& files=resourceInventory.at("files");
      const auto identity=std::find_if(files.begin(),files.end(),[name](const json& file){return file.at("path")==name;});
      if(identity==files.end())throw std::runtime_error("Required comparison resource absent from inventory");
      app->record["resourceHashes"][name]=identity->at("sha256");
    }
    const auto inventoryPath=app->output/L"resource-inventory.json";
    {std::ofstream stream(inventoryPath,std::ios::binary);stream<<resourceInventory.dump(2);if(!stream)throw std::runtime_error("Resource inventory write failed");}
    app->record["resourceInventory"]={{"file","resource-inventory.json"},{"sha256",fileHash(inventoryPath)},
      {"fileCount",resourceInventory["fileCount"]},{"totalBytes",resourceInventory["totalBytes"]}};
    wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);if(!length||length==32768)throw std::runtime_error("Executable identity");app->record["executableSha256"]=fileHash(executable);
    app->record["passScope"]="Collection guards only; no E/W/S parity or production security qualification";
    app->record["warmupMs"]=10000;app->record["durationMs"]=app->diagnosticMode?10000:30000;
    if(app->diagnosticMode)for(const auto& input:std::vector<std::pair<std::wstring,std::string>>{
        {L"diagnostic-protocol.json",diagnostic_recipe_json},{L"diagnostic-page-scripts.json",diagnostic_scripts_json},
        {L"diagnostic-probe.js",diagnostic_probe_source}}) {
      const auto file=app->output/input.first;
      {std::ofstream stream(file,std::ios::binary);stream<<input.second;if(!stream)throw std::runtime_error("Diagnostic input export failed");}
      app->record["diagnosticSourceInputs"].push_back({{"name",utf8(input.first)},{"bytes",input.second.size()},{"sha256",fileHash(file)}});
    }
    app->record["diagnosticOnly"]=app->diagnosticMode;app->save();
    const auto dpi=reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));if(dpi)dpi(reinterpret_cast<HANDLE>(-4));
    WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=&application::procedure;type.lpszClassName=L"ScratchRawWebView2Baseline";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);if(!RegisterClassW(&type))throw std::runtime_error("Window class");
    RECT bounds{0,0,app->width,app->height};if(!AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW,FALSE,0))throw std::runtime_error("Window dimensions");
    if(!CreateWindowExW(0,type.lpszClassName,L"Scratch — raw WebView2 W (F6 records loaded scene)",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,app.get()))throw std::runtime_error("Window creation");
    ShowWindow(app->window,show);app->start();MSG message{};int received{};
    while((received=GetMessageW(&message,nullptr,0,0))>0){if(message.message==WM_KEYDOWN && message.wParam==VK_F6){app->boundary([&]{app->sample();});continue;}TranslateMessage(&message);DispatchMessageW(&message);}
    result=received<0 ? 2 : static_cast<int>(message.wParam);
  }catch(const std::exception& error){if(app)app->failed(error.what());MessageBoxA(nullptr,error.what(),"Raw WebView2 baseline failed",MB_OK|MB_ICONERROR);}
  app.reset();CoUninitialize();return result;
}

#include <soui/runtime/accessibility/windows_uia_provider.h>
#include <json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <mutex>
namespace {
using namespace soui::runtime;
using json=nlohmann::json;
constexpr UINT wake=WM_APP+0x2a1;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void ok(HRESULT value,const char* message){require(value==S_OK,message);}
std::string utf8(const std::wstring& value){
  const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
  require(value.empty()||count>0,"UTF-8 conversion");std::string result(static_cast<std::size_t>(count),'\0');
  if(count)require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr)==count,"UTF-8 conversion result");return result;
}
std::wstring wide(const std::string& value){
  const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
  require(value.empty()||count>0,"UTF-16 conversion");std::wstring result(static_cast<std::size_t>(count),L'\0');
  if(count)require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count)==count,"UTF-16 conversion result");return result;
}
std::shared_ptr<const action_contract> contract(bool value){
  json properties=json::object(),required=json::array();
  if(value){properties["value"]={{"type","string"},{"maxLength",128}};required.push_back("value");}
  auto result=action_contract::parse(json{{"protocol","soui.action/1"},{"title","Native form"},{"description","Finite installed consumer"},
    {"inputSchema",{{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}}},
    {"outputSchema",{{"type","object"},{"properties",json::object()},{"required",json::array()},{"additionalProperties",false}}}}.dump());
  require(result!=nullptr,"typed native contract");return result;
}
struct application {
  HWND window{};HANDLE log{INVALID_HANDLE_VALUE};std::unique_ptr<windows_uia_provider> provider;
  uia_projection projection;std::wstring title=L"initial";std::string focused="title",choice="first";unsigned receipts{},keyboard{},uia{};
  bool removed{},closing{};DWORD owner{GetCurrentThreadId()};
  ~application(){if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);}
  void emit(json row){
    FILETIME creation{},exit{},kernel{},user{};LARGE_INTEGER qpc{};
    require(GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)&&QueryPerformanceCounter(&qpc),"host clock/identity");
    row["pid"]=GetCurrentProcessId();row["creationFileTime"]=std::to_string((static_cast<std::uint64_t>(creation.dwHighDateTime)<<32)|creation.dwLowDateTime);
    row["qpc"]=std::to_string(qpc.QuadPart);const auto bytes=row.dump()+"\n";DWORD written{};
    require(WriteFile(log,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size(),"host raw log");
  }
  uia_action_binding binding(const std::string& method,bool value=false){return {method,contract(value),"{}",value?"value":""};}
  void publish(){
    require(GetCurrentThreadId()==owner,"actual window owner");POINT origin{};RECT client{};
    require(ClientToScreen(window,&origin)&&GetClientRect(window,&client),"actual client geometry");
    projection={};projection.document_generation=1;projection.name=L"SOUI native form";projection.focused_id=focused;
    projection.screen_bounds={origin.x,origin.y,origin.x+client.right,origin.y+client.bottom};
    auto add=[&](std::string id,uia_role role,std::wstring name,int y){uia_node n;n.id=id;n.role=role;n.name=name;
      n.screen_bounds={origin.x+20,origin.y+y,origin.x+500,origin.y+y+36};projection.nodes.push_back(n);};
    if(!removed){add("title",uia_role::input,L"Native title",20);auto& n=projection.nodes.back();n.value=title;n.action=binding("form.title",true);n.focus=binding("focus.title");}
    add("choices",uia_role::selection_list,L"Priority",70);projection.nodes.back().screen_bounds.bottom=origin.y+165;
    add("first",uia_role::selection_item,L"Normal priority",85);projection.nodes.back().parent_id="choices";projection.nodes.back().selected=choice=="first";projection.nodes.back().action=binding("form.first");
    add("second",uia_role::selection_item,L"High priority",125);projection.nodes.back().parent_id="choices";projection.nodes.back().selected=choice=="second";projection.nodes.back().action=binding("form.second");
    add("commit",uia_role::button,L"Commit form",180);projection.nodes.back().action=binding("form.commit");projection.nodes.back().focus=binding("focus.commit");
    add("status",uia_role::task_status,L"Form receipt",230);projection.nodes.back().status=wide("receipt="+std::to_string(receipts)+";title="+utf8(title)+";priority="+choice);
    add("secret",uia_role::input,L"Private credential",280);projection.nodes.back().secret=true;
    add("remove",uia_role::button,L"Remove title",330);projection.nodes.back().action=binding("form.remove");projection.nodes.back().focus=binding("focus.remove");
    ok(provider->publish(projection),"publish real native state");InvalidateRect(window,nullptr,TRUE);
  }
  uia_action_result act(const std::string& method,const std::string& parameters,const char* input){
    require(GetCurrentThreadId()==owner,"business executes on owner");
    const auto data=json::parse(parameters);
    if(method=="form.title"){
      const auto next=data.at("value").get<std::string>();if(removed||next.size()>128)return {E_INVALIDARG,"{}"};title=wide(next);
    }else if(method=="form.first")choice="first";
    else if(method=="form.second")choice="second";
    else if(method=="form.commit"){if(title.empty())return {E_INVALIDARG,"{}"};++receipts;}
    else if(method=="form.remove"){removed=true;if(focused=="title")focused="commit";}
    else if(method.rfind("focus.",0)==0){focused=method.substr(6);SetFocus(window);require(GetFocus()==window,"actual HWND keyboard focus");}
    else return {E_ACCESSDENIED,"{}"};
    if(std::string(input)=="keyboard")++keyboard;else ++uia;
    publish();emit({{"event","business"},{"method",method},{"input",input},{"title",utf8(title)},{"choice",choice},{"receipts",receipts},{"focused",focused},{"removed",removed}});return {S_OK,"{}"};
  }
  void key(WPARAM key){
    if(key==VK_TAB){const auto next=focused=="title"?"commit":focused=="commit"?"remove":removed?"commit":"title";(void)act(std::string("focus.")+next,"{}","keyboard");}
    if(key==VK_RETURN&&(focused=="commit"||focused=="remove"))(void)act("form."+focused,"{}","keyboard");
  }
  void character(wchar_t key){if(focused!="title"||removed||key<32||key>126)return;const auto next=title+key;if(utf8(next).size()<=128)(void)act("form.title",json{{"value",utf8(next)}}.dump(),"keyboard");}
  void close(){closing=true;const auto result=provider->close();require(result==S_OK||result==S_FALSE,"provider close");if(result==S_FALSE){SetTimer(window,2,10,nullptr);return;}provider.reset();emit({{"event","closed"},{"receipts",receipts},{"keyboardOperations",keyboard},{"uiaOperations",uia}});DestroyWindow(window);window=nullptr;PostQuitMessage(0);}
};
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM w,LPARAM l){
  if(message==WM_NCCREATE)SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
  auto* a=reinterpret_cast<application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  try{
    if(a&&a->provider){LRESULT result{};if(a->provider->handle_message(message,w,l,result)){if(message==wake)a->emit({{"event","provider-wake"},{"result",static_cast<std::uint32_t>(result)}});return result;}}
    if(a&&message==WM_CLOSE){a->close();return 0;}
    if(a&&message==WM_TIMER){if(w==2&&a->closing)a->close();else if(w==1)throw std::runtime_error("host 80-second deadline");return 0;}
    if(a&&message==WM_KEYDOWN){a->key(w);return 0;}
    if(a&&message==WM_CHAR){a->character(static_cast<wchar_t>(w));return 0;}
    if(a&&message==WM_PAINT){PAINTSTRUCT p{};HDC dc=BeginPaint(window,&p);POINT origin{};ClientToScreen(window,&origin);
      for(const auto& n:a->projection.nodes){if(n.role==uia_role::selection_list)continue;RECT r=n.screen_bounds;OffsetRect(&r,-origin.x,-origin.y);Rectangle(dc,r.left,r.top,r.right,r.bottom);
        const auto text=n.name+(n.secret?L" ******":n.role==uia_role::input?L": "+n.value:n.role==uia_role::task_status?L": "+n.status:n.selected?L" [selected]":L"");
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);if(n.id==a->focused)DrawFocusRect(dc,&r);}
      EndPaint(window,&p);return 0;}
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';PostQuitMessage(1);return 0;}
  return DefWindowProcW(window,message,w,l);
}
}
namespace {
HANDLE focus_trace_file=INVALID_HANDLE_VALUE;
std::mutex focus_trace_mutex;
struct close_focus_trace {~close_focus_trace(){if(focus_trace_file!=INVALID_HANDLE_VALUE)CloseHandle(focus_trace_file);}};
}
extern "C" void SouiPrivateFocusTrace(const char* phase,bool root,DWORD caller,DWORD owner,HRESULT result) noexcept {
  try {std::lock_guard<std::mutex> lock(focus_trace_mutex);if(focus_trace_file==INVALID_HANDLE_VALUE)return;
    const auto bytes=nlohmann::json{{"event","provider-focus-diagnostic"},{"phase",phase},{"root",root},{"callerThread",caller},{"ownerThread",owner},{"hresult",static_cast<std::uint32_t>(result)}}.dump()+"\n";
    DWORD written{};(void)WriteFile(focus_trace_file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);
  }catch(...){}
}
int wmain(int argc,wchar_t** argv){
  close_focus_trace trace_lifetime;
  if(argc==2){const auto trace_path=std::wstring(argv[1])+L".focus.ndjson";focus_trace_file=CreateFileW(trace_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(focus_trace_file==INVALID_HANDLE_VALUE)return 1;}
  const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;int code=1;
  try{
    require(argc==2,"host requires new absolute log path");require(SetProcessDPIAware()!=FALSE,"DPI awareness");application app;
    app.log=CreateFileW(argv[1],GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);require(app.log!=INVALID_HANDLE_VALUE,"new host log");
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"SouiInstalledNativeUiaForm";
    require(RegisterClassW(&cls)!=0,"native class");
    app.window=CreateWindowExW(0,cls.lpszClassName,L"SOUI installed native UIA form",WS_OVERLAPPEDWINDOW,100,100,620,500,nullptr,nullptr,cls.hInstance,&app);require(app.window!=nullptr,"actual native HWND");
    ok(windows_uia_provider::create(app.window,wake,1,[&](const uia_action_request& r){const bool allowed=!r.native_user_gesture()&&r.document_generation==1&&!app.closing;app.emit({{"event","uia-authorize"},{"method",r.method},{"allowed",allowed}});return allowed;},
      [&](const uia_action_request& r){return app.act(r.method,r.parameters_json,"uia");},app.provider),"installed optional UIA create");
    app.publish();ShowWindow(app.window,SW_SHOW);UpdateWindow(app.window);SetTimer(app.window,1,80000,nullptr);
    app.emit({{"event","ready"},{"hwnd",std::to_string(reinterpret_cast<std::uintptr_t>(app.window))}});
    MSG m{};BOOL status{};while((status=GetMessageW(&m,nullptr,0,0))>0){TranslateMessage(&m);DispatchMessageW(&m);}require(status==0,"message loop");code=static_cast<int>(m.wParam);
    if(app.provider){const auto closed=app.provider->close();require(closed==S_OK,"failure path provider close");app.provider.reset();}if(app.window)DestroyWindow(app.window);
    require(UnregisterClassW(cls.lpszClassName,cls.hInstance)!=FALSE,"class cleanup");
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';code=1;}
  CoUninitialize();return code;
}

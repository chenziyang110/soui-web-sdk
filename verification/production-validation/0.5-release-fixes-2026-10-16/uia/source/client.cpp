#include "public-endpoint-client.h"
#include <UIAutomation.h>
#include <objbase.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <utility>
using public_endpoint_tests::handle;
using public_endpoint_tests::json;
using public_endpoint_tests::require;
namespace {
ULONGLONG workflow_deadline{};
void deadline(){require(GetTickCount64()<workflow_deadline,"60-second native UIA workflow deadline");}
std::uint64_t creation(HANDLE p){FILETIME a{},b{},c{},d{};require(GetProcessTimes(p,&a,&b,&c,&d)!=FALSE,"held process identity");return (static_cast<std::uint64_t>(a.dwHighDateTime)<<32)|a.dwLowDateTime;}
std::wstring quote(const std::wstring& input){std::wstring result=L"\"";unsigned slashes{};for(const auto c:input){if(c==L'\\'){++slashes;continue;}result.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;if(c==L'"')result+=L'\\';result+=c;}result.append(slashes*2,L'\\');return result+L'"';}
void emit(json row){LARGE_INTEGER tick{};require(QueryPerformanceCounter(&tick)!=FALSE,"QPC");row["qpc"]=std::to_string(tick.QuadPart);std::cout<<row.dump()<<'\n'<<std::flush;}
#include "screen-reader.h"
template<class T>struct com_ref{T* p{};~com_ref(){if(p)p->Release();}com_ref()=default;com_ref(const com_ref&)=delete;com_ref& operator=(const com_ref&)=delete;com_ref(com_ref&& x)noexcept:p(std::exchange(x.p,nullptr)){}T** put(){if(p)p->Release();p=nullptr;return &p;}T* operator->()const{return p;}};
void ok(HRESULT value,const char* message){if(value!=S_OK)emit({{"event","hresult-diagnostic"},{"operation",message},{"hresult",static_cast<std::uint32_t>(value)}});require(value==S_OK,message);}
struct search{DWORD pid{};HWND window{};bool duplicate{};};
BOOL CALLBACK find_window(HWND window,LPARAM context){auto& s=*reinterpret_cast<search*>(context);DWORD pid{};GetWindowThreadProcessId(window,&pid);wchar_t name[128]{};GetClassNameW(window,name,128);if(pid==s.pid&&IsWindowVisible(window)&&wcscmp(name,L"SouiInstalledNativeUiaForm")==0){if(s.window){s.duplicate=true;return FALSE;}s.window=window;}return TRUE;}
com_ref<IUIAutomationElement> find(IUIAutomation* client,IUIAutomationElement* root,const wchar_t* id){
  VARIANT value{};value.vt=VT_BSTR;value.bstrVal=SysAllocString(id);require(value.bstrVal!=nullptr,"automation id");com_ref<IUIAutomationCondition> condition;
  const auto result=client->CreatePropertyCondition(UIA_AutomationIdPropertyId,value,condition.put());VariantClear(&value);ok(result,"UIA id condition");
  com_ref<IUIAutomationElement> element;ok(root->FindFirst(TreeScope_Descendants,condition.p,element.put()),"UIA descendant lookup");return element;
}
template<class T>com_ref<T> pattern(IUIAutomationElement* node,PATTERNID id){com_ref<T> result;ok(node->GetCurrentPatternAs(id,__uuidof(T),reinterpret_cast<void**>(result.put())),"actual UIA pattern");require(result.p!=nullptr,"pattern present");return result;}
HRESULT set_value(IUIAutomationValuePattern* pattern,const wchar_t* text){BSTR encoded=SysAllocString(text);require(encoded!=nullptr,"actual BSTR input");const auto result=pattern->SetValue(encoded);SysFreeString(encoded);return result;}
std::wstring value(IUIAutomationElement* node){auto p=pattern<IUIAutomationValuePattern>(node,UIA_ValuePatternId);BSTR text{};ok(p->get_CurrentValue(&text),"UIA value");std::wstring result(text?text:L"",text?SysStringLen(text):0);SysFreeString(text);return result;}
std::wstring item_status(IUIAutomationElement* node){BSTR text{};ok(node->get_CurrentItemStatus(&text),"UIA status");std::wstring result(text?text:L"",text?SysStringLen(text):0);SysFreeString(text);return result;}
template<class F>void until(F predicate,const char* message){const auto end=GetTickCount64()+3000;while(GetTickCount64()<end){deadline();if(predicate())return;Sleep(20);}throw std::runtime_error(message);}
void owned_window(HWND window,HANDLE process,DWORD pid){DWORD actual{};GetWindowThreadProcessId(window,&actual);require(process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT&&actual==pid&&IsWindow(window),"held native window/process no longer matches");}
void foreground(HWND window,HANDLE process,DWORD pid){
  owned_window(window,process,pid);ShowWindow(window,SW_SHOW);
  if(!SetForegroundWindow(window)&&GetForegroundWindow()!=window){
    // Windows can deny a background process's activation request. Exercise a
    // real bounded click on this owned title bar; never inject into another HWND.
    for(const int key:{VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN})
      require((GetAsyncKeyState(key)&0x8000)==0,"user input is active before owned-window activation");
    const bool wasTopmost=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    struct restore_topmost {HWND window;HANDLE process;DWORD pid;bool restore;~restore_topmost(){DWORD actual{};GetWindowThreadProcessId(window,&actual);if(restore&&WaitForSingleObject(process,0)==WAIT_TIMEOUT&&actual==pid&&IsWindow(window))SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}} guard{window,process,pid,!wasTopmost};
    require(SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"owned title bar visibility");
    RECT rect{};POINT saved{};require(GetWindowRect(window,&rect)&&GetCursorPos(&saved),"owned activation geometry");
    POINT point{rect.left+(rect.right-rect.left)/2,rect.top+GetSystemMetrics(SM_CYCAPTION)/2+GetSystemMetrics(SM_CYFRAME)};
    owned_window(window,process,pid);require(GetAncestor(WindowFromPoint(point),GA_ROOT)==window,"activation hit belongs to another window");
    const int left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN),width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    require(width>1&&height>1&&point.x>=left&&point.x<left+width&&point.y>=top&&point.y<top+height,"activation point outside desktop");
    INPUT input[3]{};for(auto& item:input)item.type=INPUT_MOUSE;
    input[0].mi.dx=static_cast<LONG>((static_cast<long long>(point.x-left)*65535)/(width-1));
    input[0].mi.dy=static_cast<LONG>((static_cast<long long>(point.y-top)*65535)/(height-1));
    input[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    input[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;input[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    owned_window(window,process,pid);require(GetAncestor(WindowFromPoint(point),GA_ROOT)==window,"owned activation target changed");
    const auto inserted=SendInput(3,input,sizeof(INPUT));
    if(inserted==2){INPUT release{};release.type=INPUT_MOUSE;release.mi.dwFlags=MOUSEEVENTF_LEFTUP;(void)SendInput(1,&release,sizeof(INPUT));}
    require(inserted==3,"owned activation click incomplete");
    until([&]{owned_window(window,process,pid);return GetForegroundWindow()==window;},"owned activation click did not focus application");
    POINT current{};if(GetCursorPos(&current)&&abs(current.x-point.x)<=1&&abs(current.y-point.y)<=1)(void)SetCursorPos(saved.x,saved.y);
    emit({{"event","owned-titlebar-click"},{"appPid",pid},{"physicalInputQualified",false}});
  }
  until([&]{owned_window(window,process,pid);return GetForegroundWindow()==window;},"foreground activation incomplete");
}
void key(HWND window,HANDLE process,DWORD pid,WORD code,bool unicode=false){owned_window(window,process,pid);require(GetForegroundWindow()==window,"keyboard owner changed");INPUT values[2]{};for(auto& item:values){item.type=INPUT_KEYBOARD;item.ki.wVk=unicode?0:code;item.ki.wScan=unicode?code:0;item.ki.dwFlags=unicode?KEYEVENTF_UNICODE:0;}values[1].ki.dwFlags|=KEYEVENTF_KEYUP;require(SendInput(2,values,sizeof(INPUT))==2,"actual keyboard delivery");emit({{"event","keyboard-sent"},{"code",code},{"unicode",unicode},{"physicalInputQualified",false}});}
void focus(IUIAutomationElement* node,screen_reader& reader,const char* label){const auto offset=reader.offset();ok(node->SetFocus(),"native provider focus");until([&]{BOOL focused{};return node->get_CurrentHasKeyboardFocus(&focused)==S_OK&&focused;},"native focus projection");reader.expect_spoken(offset,label);}
}
int wmain(int argc,wchar_t** argv){
  HANDLE appHandle{};DWORD pid{};HWND window{};screen_reader reader;bool com=false;
  try{
    require(argc==6,"Usage: client HOST HOST_LOG NVDA CONFIG NVDA_LOG");ok(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"client MTA");com=true;
    reader.preflight();BOOL owned{};require(IsProcessInJob(GetCurrentProcess(),nullptr,&owned)&&owned,"outer Owned Job required");
    const std::wstring app=argv[1];std::wstring command=quote(app)+L" "+quote(argv[2]);STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    require(CreateProcessW(app.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,app.substr(0,app.find_last_of(L"\\/")).c_str(),&startup,&process)!=FALSE,"native host creation");
    appHandle=process.hProcess;pid=process.dwProcessId;handle thread{process.hThread};const auto ft=creation(appHandle);BOOL childOwned{};
    require(IsProcessInJob(appHandle,nullptr,&childOwned)&&childOwned,"host inherited Owned Job");
    emit({{"event","app-created"},{"coordinatorPid",GetCurrentProcessId()},{"coordinatorCreationFileTime",std::to_string(creation(GetCurrentProcess()))},{"appPid",pid},{"appCreationFileTime",std::to_string(ft)},{"owned",true}});
    workflow_deadline=GetTickCount64()+60000;require(ResumeThread(thread.get())!=static_cast<DWORD>(-1),"host resume");
    until([&]{require(WaitForSingleObject(appHandle,0)==WAIT_TIMEOUT,"host exited before HWND");search s{pid};EnumWindows(find_window,reinterpret_cast<LPARAM>(&s));require(!s.duplicate,"one native app window");window=s.window;return window!=nullptr;},"native window unavailable");
    reader.start(argv[3],argv[4],argv[5]);foreground(window,appHandle,pid);
    {
      com_ref<IUIAutomation> automation;ok(CoCreateInstance(__uuidof(CUIAutomation),nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUIAutomation),reinterpret_cast<void**>(automation.put())),"actual Windows UIA activation");
      com_ref<IUIAutomationElement> root;ok(automation->ElementFromHandle(window,root.put()),"real native UIA root");
      int processId{};ok(root->get_CurrentProcessId(&processId),"UIA root process");require(processId==static_cast<int>(pid),"root native PID");
      auto title=find(automation.p,root.p,L"title"),second=find(automation.p,root.p,L"second"),commit=find(automation.p,root.p,L"commit"),status=find(automation.p,root.p,L"status"),secret=find(automation.p,root.p,L"secret"),remove=find(automation.p,root.p,L"remove");
      require(title.p&&second.p&&commit.p&&status.p&&secret.p&&remove.p,"actual native controls present");
      // NVDA may already have announced the owner's initial title focus during
      // startup. Require a real transition before capturing a new announcement.
      // Wait for NVDA to observe the first transition: SDK notifications may
      // legitimately coalesce intermediate states before the next owner pump.
      focus(commit.p,reader,"Commit form");
      focus(title.p,reader,"Native title");
      auto titleValue=pattern<IUIAutomationValuePattern>(title.p,UIA_ValuePatternId);ok(set_value(titleValue.p,L"Native accepted"),"typed native SetValue");require(value(title.p)==L"Native accepted","committed input value");
      auto selection=pattern<IUIAutomationSelectionItemPattern>(second.p,UIA_SelectionItemPatternId);ok(selection->Select(),"typed native Select");BOOL selected{};ok(selection->get_CurrentIsSelected(&selected),"selected projection");require(selected,"native choice committed");
      auto invoke=pattern<IUIAutomationInvokePattern>(commit.p,UIA_InvokePatternId);ok(invoke->Invoke(),"typed native Invoke");
      until([&]{return item_status(status.p)==L"receipt=1;title=Native accepted;priority=second";},"native form receipt missing");
      emit({{"event","uia-form-committed"},{"receipt",1},{"title","Native accepted"},{"choice","second"}});
      ok(title.p->SetFocus(),"keyboard field focus");key(window,appHandle,pid,L'K',true);until([&]{return value(title.p)==L"Native acceptedK";},"keyboard character did not enter native form");
      const auto commitOffset=reader.offset();key(window,appHandle,pid,VK_TAB);until([&]{BOOL focused{};return commit->get_CurrentHasKeyboardFocus(&focused)==S_OK&&focused;},"Tab did not move native focus");reader.expect_spoken(commitOffset,"Commit form");
      key(window,appHandle,pid,VK_RETURN);until([&]{return item_status(status.p)==L"receipt=2;title=Native acceptedK;priority=second";},"Enter did not commit same native form");
      emit({{"event","keyboard-form-committed"},{"receipt",2},{"title","Native acceptedK"},{"choice","second"}});
      BOOL password{};ok(secret->get_CurrentIsPassword(&password),"password property");require(password,"sensitive field not marked");
      IUnknown* unavailable{};const auto patternStatus=secret->GetCurrentPattern(UIA_ValuePatternId,&unavailable);const bool absent=unavailable==nullptr;if(unavailable)unavailable->Release();require((patternStatus==static_cast<HRESULT>(UIA_E_NOTSUPPORTED)||patternStatus==S_OK)&&absent,"secret Value pattern exposed");
      VARIANT hidden{};ok(secret->GetCurrentPropertyValue(UIA_ValueValuePropertyId,&hidden),"secret property query");
      BOOL unsupported{};ok(automation->CheckNotSupported(hidden,&unsupported),"reserved unsupported property identity");
      const bool redacted=unsupported||hidden.vt==VT_EMPTY||(hidden.vt==VT_BSTR&&SysStringLen(hidden.bstrVal)==0);VariantClear(&hidden);require(redacted,"secret value disclosed");
      emit({{"event","secret-redacted"},{"password",true},{"valuePatternExposed",false}});
      focus(remove.p,reader,"Remove title");auto remover=pattern<IUIAutomationInvokePattern>(remove.p,UIA_InvokePatternId);ok(remover->Invoke(),"remove actual dynamic control");
      until([&]{return !find(automation.p,root.p,L"title").p;},"removed title still discoverable");
      const HRESULT staleStatus=set_value(titleValue.p,L"stale mutation"); emit({{"event","stale-result-diagnostic"},{"hresult",static_cast<unsigned long>(staleStatus)}}); require(staleStatus==static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE)||staleStatus==static_cast<HRESULT>(UIA_E_INVALIDOPERATION),"removed UIA control did not reject mutation");
      emit({{"event","removed-control-rejected"},{"staleMutationAccepted",false}});
    }
    reader.stop();owned_window(window,appHandle,pid);require(PostMessageW(window,WM_CLOSE,0,0)!=FALSE,"native close request");
    require(WaitForSingleObject(appHandle,6000)==WAIT_OBJECT_0,"native host did not close naturally");DWORD code{};require(GetExitCodeProcess(appHandle,&code)&&code==0&&creation(appHandle)==ft,"native host exit identity");
    emit({{"event","complete"},{"appPid",pid},{"appCreationFileTime",std::to_string(ft)},{"appExitCode",code},{"nativeProviderWorkflowPassed",true},{"actualSpeechTextPassed",true},{"physicalInputQualified",false},{"releaseReady",false}});
    CloseHandle(appHandle);appHandle=nullptr;CoUninitialize();return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window&&appHandle&&WaitForSingleObject(appHandle,0)==WAIT_TIMEOUT){DWORD owner{};GetWindowThreadProcessId(window,&owner);if(owner==pid)PostMessageW(window,WM_CLOSE,0,0);}if(appHandle){(void)WaitForSingleObject(appHandle,2000);CloseHandle(appHandle);}if(com)CoUninitialize();return 1;}
}

#pragma once
// A separately owned, unmodified NVDA distribution supplies real speech output.
// Nothing writes to its log except NVDA. Text recognition is not audible speech.
class screen_reader final {
 public:
  ~screen_reader() { request_stop(); if(process_){(void)WaitForSingleObject(process_,2000);CloseHandle(process_);} }
  void preflight() {
    require(!FindWindowW(L"wxWindowClassNR",L"NVDA"),"preserve existing NVDA instance");
    handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    require(snapshot.get()!=INVALID_HANDLE_VALUE,"screen reader process preflight unavailable");
    PROCESSENTRY32W row{};row.dwSize=sizeof(row);
    require(Process32FirstW(snapshot.get(),&row)!=FALSE,"process inventory unavailable");
    do {
      for(const auto* name:{L"nvda.exe",L"nvda_noUIAccess.exe",L"nvda_uiAccess.exe",L"Narrator.exe"})
        require(_wcsicmp(row.szExeFile,name)!=0,"preserve user's running screen reader");
    } while(Process32NextW(snapshot.get(),&row));
    require(GetLastError()==ERROR_NO_MORE_FILES,"incomplete process inventory");
  }
  void start(const wchar_t* executable,const wchar_t* config,const wchar_t* log) {
    preflight();log_=log;
    require(!std::filesystem::exists(log_),"fresh original NVDA log required");
    const std::wstring file=executable;
    // This upstream option exits if an NVDA window appears concurrently, instead
    // of the normal launcher behavior that stops an existing instance.
    std::wstring command=quote(file)+L" --minimal --no-sr-flag --disable-addons --ease-of-access --log-level=12 --lang=en --config-path="+quote(config)+L" --log-file="+quote(log);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    const auto cwd=file.substr(0,file.find_last_of(L"\\/"));
    require(CreateProcessW(file.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,cwd.c_str(),&startup,&child)!=FALSE,"actual NVDA creation failed");
    process_=child.hProcess;pid_=child.dwProcessId;handle thread{child.hThread};
    BOOL owned=FALSE;require(IsProcessInJob(process_,nullptr,&owned)&&owned,"NVDA escaped owned job");
    created_=creation(process_);
    emit({{"event","screenreader-created"},{"pid",pid_},{"creationFileTime",std::to_string(created_)},{"owned",true},{"configuredSynth","silence"}});
    require(ResumeThread(thread.get())!=static_cast<DWORD>(-1),"NVDA resume failed");
    const auto limit=GetTickCount64()+20000;
    while(GetTickCount64()<limit){
      deadline();require(WaitForSingleObject(process_,0)==WAIT_TIMEOUT,"NVDA exited during initialization");
      if(read().find("NVDA initialized")!=std::string::npos){
        HWND window=FindWindowW(L"wxWindowClassNR",L"NVDA");DWORD owner{};if(window)GetWindowThreadProcessId(window,&owner);
        require(window&&owner==pid_,"actual NVDA window identity differs");window_=window;
        emit({{"event","screenreader-ready"},{"pid",pid_},{"hwnd",std::to_string(reinterpret_cast<std::uintptr_t>(window))}});return;
      }
      Sleep(25);
    }
    throw std::runtime_error("actual NVDA initialization exceeded 20 seconds");
  }
  std::size_t offset() { alive();return read().size(); }
  void expect_spoken(std::size_t begin,const std::string& expected) {
    const auto limit=GetTickCount64()+4000;
    while(GetTickCount64()<limit){
      deadline();alive();const auto bytes=read();require(bytes.size()>=begin,"NVDA original log was truncated");
      std::size_t at=begin;
      while((at=bytes.find("Speaking [",at))!=std::string::npos){
        const auto end=bytes.find('\n',at);if(end==std::string::npos)break;
        if(bytes.substr(at,end-at).find(expected)!=std::string::npos){
          emit({{"event","screenreader-spoken"},{"pid",pid_},{"logOffset",at},{"logEnd",end},{"expectedLabel",expected},{"actualLine",bytes.substr(at,end-at)},{"audibleSpeechValidated",false}});return;
        }
        at=end+1;
      }
      Sleep(25);
    }
    throw std::runtime_error("actual NVDA speech text did not contain the focused control name");
  }
  void stop() {
    alive();require(request_stop(),"owned NVDA quit request failed");
    require(WaitForSingleObject(process_,6000)==WAIT_OBJECT_0,"NVDA did not quit naturally");
    DWORD code=STILL_ACTIVE;require(GetExitCodeProcess(process_,&code)&&code==0&&creation(process_)==created_,"NVDA natural exit identity/status differs");
    emit({{"event","screenreader-stopped"},{"pid",pid_},{"creationFileTime",std::to_string(created_)},{"exitCode",code}});
    CloseHandle(process_);process_=nullptr;window_=nullptr;
  }
 private:
  void alive() { require(process_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT&&creation(process_)==created_,"owned NVDA is no longer alive"); }
  std::string read() {
    HANDLE raw=CreateFileW(log_.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(raw==INVALID_HANDLE_VALUE){require(GetLastError()==ERROR_FILE_NOT_FOUND,"NVDA log read failed");return {};}
    handle file{raw};LARGE_INTEGER size{};require(GetFileSizeEx(file.get(),&size)&&size.QuadPart>=0&&size.QuadPart<=8*1024*1024,"NVDA log exceeds bounded capture");
    std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0');DWORD count{};
    require(ReadFile(file.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size(),"NVDA log capture incomplete");return bytes;
  }
  bool request_stop() noexcept {
    if(!process_||WaitForSingleObject(process_,0)!=WAIT_TIMEOUT)return false;
    HWND window=window_?window_:FindWindowW(L"wxWindowClassNR",L"NVDA");DWORD owner{};
    if(window)GetWindowThreadProcessId(window,&owner);
    return window&&owner==pid_&&PostMessageW(window,WM_QUIT,0,0)!=FALSE;
  }
  HANDLE process_{};DWORD pid_{};std::uint64_t created_{};HWND window_{};std::wstring log_;
};
screen_reader* active_reader{};

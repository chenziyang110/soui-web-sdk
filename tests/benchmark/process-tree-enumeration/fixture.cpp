#define SOUI_PROCESS_TREE_SAMPLER_CONTRACT
#include "../../../benchmarks/protocol/process-tree-sampler.cpp"

#include <exception>
#include <initializer_list>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

struct fake_toolhelp {
  HANDLE snapshot_value{reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x1234))};
  DWORD snapshot_error{ERROR_ACCESS_DENIED};
  DWORD first_error{ERROR_NO_MORE_FILES};
  DWORD next_error{ERROR_NO_MORE_FILES};
  DWORD close_error{ERROR_INVALID_HANDLE};
  DWORD error{ERROR_NO_MORE_FILES}; // A stale 18 must be cleared before each API.
  bool first_fails{};
  bool close_fails{};
  bool first_sets_error{true};
  bool next_sets_error{true};
  bool close_sets_error{true};
  unsigned snapshots{}, firsts{}, nexts{}, closes{}, clear_calls{};
  bool size_valid{true};
  bool correct_handle{true};
  std::vector<PROCESSENTRY32W> entries;
  std::size_t cursor{};

  explicit fake_toolhelp(std::initializer_list<DWORD> pids = {}) {
    for (auto pid : pids) {
      PROCESSENTRY32W item{};
      item.dwSize = sizeof(item);
      item.th32ProcessID = pid;
      item.th32ParentProcessID = 1;
      entries.push_back(item);
    }
  }
  void clear_error() noexcept { error = ERROR_SUCCESS; ++clear_calls; }
  DWORD last_error() const noexcept { return error; }
  HANDLE snapshot() noexcept {
    ++snapshots;
    if (!snapshot_value || snapshot_value == INVALID_HANDLE_VALUE) error = snapshot_error;
    return snapshot_value;
  }
  BOOL first(HANDLE value, PROCESSENTRY32W* entry) noexcept {
    ++firsts;
    correct_handle = correct_handle && value == snapshot_value;
    size_valid = size_valid && entry->dwSize == sizeof(*entry);
    if (first_fails || entries.empty()) {
      if (first_sets_error) error = first_error;
      return FALSE;
    }
    cursor = 0;
    *entry = entries[cursor];
    return TRUE;
  }
  BOOL next(HANDLE value, PROCESSENTRY32W* entry) noexcept {
    ++nexts;
    correct_handle = correct_handle && value == snapshot_value;
    size_valid = size_valid && entry->dwSize == sizeof(*entry);
    if (++cursor >= entries.size()) {
      if (next_sets_error) error = next_error;
      return FALSE;
    }
    *entry = entries[cursor];
    return TRUE;
  }
  BOOL close(HANDLE value) noexcept {
    ++closes;
    correct_handle = correct_handle && value == snapshot_value;
    // Overwrite last error even on success, testing immediate error capture.
    if (close_sets_error) error = close_fails ? close_error : ERROR_BAD_LENGTH;
    return close_fails ? FALSE : TRUE;
  }
};

enumeration_status enumerate(fake_toolhelp& api, std::vector<DWORD>& seen) {
  return enumerate_process_snapshot(api, [&](const PROCESSENTRY32W& entry) {
    seen.push_back(entry.th32ProcessID);
  });
}

unsigned passed{};
template<class Test>
void test(Test&& body) { body(); ++passed; }
}

int main() {
  try {
    test([] {
      fake_toolhelp api{11, 22, 33}; std::vector<DWORD> seen;
      const auto status = enumerate(api, seen);
      require(status.complete() && status.exhausted && status.error == 0 && !status.close_failed && status.close_error == 0,
              "ERROR_NO_MORE_FILES must complete normal enumeration");
      require(seen == std::vector<DWORD>({11, 22, 33}) && api.closes == 1 && api.nexts == 3 &&
              api.size_valid && api.correct_handle, "Entry order/size/handle/close changed");
    });
    test([] {
      fake_toolhelp api{11, 22}; api.next_error = ERROR_ACCESS_DENIED; std::vector<DWORD> seen;
      const auto status = enumerate(api, seen);
      require(!status.complete() && !status.exhausted && status.failure == enumeration_failure::next &&
              status.error == ERROR_ACCESS_DENIED && status.close_error == 0 && api.closes == 1,
              "A truncated snapshot was accepted or its original error was lost during close");
    });
    test([] {
      fake_toolhelp api{11}; api.next_sets_error = false; std::vector<DWORD> seen;
      const auto status = enumerate(api, seen);
      require(!status.complete() && status.failure == enumeration_failure::next && status.error == 0,
              "False Next with no new error must not inherit stale ERROR_NO_MORE_FILES");
    });
    test([] {
      fake_toolhelp api{11}; api.first_fails = true; api.first_error = ERROR_BAD_LENGTH; std::vector<DWORD> seen;
      const auto status = enumerate(api, seen);
      require(!status.complete() && status.failure == enumeration_failure::first && status.error == ERROR_BAD_LENGTH &&
              seen.empty() && api.nexts == 0 && api.closes == 1, "First failure must remain an incomplete enumeration");
    });
    test([] {
      fake_toolhelp api{11}; api.first_fails = true; api.first_sets_error = false; std::vector<DWORD> seen;
      const auto status = enumerate(api, seen);
      require(!status.complete() && status.failure == enumeration_failure::first && status.error == 0,
              "False First without last error must remain failed");
    });
    test([] {
      fake_toolhelp api; std::vector<DWORD> seen;
      const auto status = enumerate(api, seen);
      require(status.complete() && status.exhausted && seen.empty() && api.nexts == 0 && api.closes == 1,
              "First ERROR_NO_MORE_FILES must be classified as an empty enumeration");
      fake_toolhelp empty;
      const auto value = collect(GetCurrentProcessId(), 1, {}, empty);
      require(value.enumeration.complete() && !value.complete && value.members.empty(),
              "An empty snapshot must not be a complete root-process sample");
    });
    test([] {
      for (const auto invalid : {INVALID_HANDLE_VALUE, static_cast<HANDLE>(nullptr)}) {
        fake_toolhelp api{11}; api.snapshot_value = invalid; std::vector<DWORD> seen;
        const auto status = enumerate(api, seen);
        require(!status.complete() && status.failure == enumeration_failure::snapshot &&
                status.error == ERROR_ACCESS_DENIED && api.firsts == 0 && api.closes == 0 && seen.empty(),
                "An invalid snapshot must fail without reading or closing it");
      }
    });
    test([] {
      fake_toolhelp api{11}; api.close_fails = true; std::vector<DWORD> seen;
      const auto status = enumerate(api, seen);
      require(!status.complete() && status.exhausted && status.failure == enumeration_failure::close &&
              status.error == ERROR_INVALID_HANDLE && status.close_error == ERROR_INVALID_HANDLE && api.closes == 1,
              "Close failure must be in returned status before return (independent of NRVO)");
    });
    test([] {
      fake_toolhelp api{11}; api.close_fails = true; api.close_sets_error = false; std::vector<DWORD> seen;
      const auto status = enumerate(api, seen);
      require(!status.complete() && status.failure == enumeration_failure::close && status.close_failed && status.error == 0 && api.closes == 1,
              "Close false with error zero must not be accepted or retried");
    });
    test([] {
      for (const DWORD close_error : {DWORD{ERROR_INVALID_HANDLE}, DWORD{ERROR_SUCCESS}}) {
        fake_toolhelp api{11}; api.next_error = ERROR_ACCESS_DENIED; api.close_fails = true; api.close_error = close_error;
        std::vector<DWORD> seen; const auto status = enumerate(api, seen);
        require(status.failure == enumeration_failure::next && status.error == ERROR_ACCESS_DENIED &&
                status.close_failed && status.close_error == close_error && api.closes == 1,
                "Secondary close failure (even error zero) must not overwrite the original Next error");
      }
    });
    test([] {
      fake_toolhelp api{11}; api.first_fails = true; api.first_error = ERROR_BAD_LENGTH; api.close_fails = true;
      std::vector<DWORD> seen; const auto status = enumerate(api, seen);
      require(status.failure == enumeration_failure::first && status.error == ERROR_BAD_LENGTH &&
              status.close_error == ERROR_INVALID_HANDLE && api.closes == 1,
              "Secondary close error must not overwrite the original First error");
    });
    test([] {
      struct visitor_failure {};
      fake_toolhelp api{11}; api.close_fails = true; bool original = false;
      try { (void)enumerate_process_snapshot(api, [](const PROCESSENTRY32W&) { throw visitor_failure{}; }); }
      catch (const visitor_failure&) { original = true; }
      require(original && api.closes == 1 && api.nexts == 0, "Unwind must preserve visitor failure and close exactly once");
    });
    test([] {
      // If partial entries were used, the real current process could be opened
      // and incorrectly appear as a valid root. Failure must discard the tree.
      fake_toolhelp api{GetCurrentProcessId()}; api.next_error = ERROR_BAD_LENGTH;
      const auto value = collect(GetCurrentProcessId(), 0, {}, api);
      require(!value.complete && !value.enumeration.complete() && value.members.empty() &&
              value.private_bytes == 0 && value.working_set_bytes == 0,
              "A partial snapshot must not be used to establish process-tree membership");
      const auto line = encode_sample(value, GetCurrentProcessId(), 1, 0, 0, false);
      require(line.find("\"complete\":false") != std::string::npos &&
              line.find("\"failure\":\"next\",\"win32Error\":24,\"closeFailed\":false,\"closeError\":0") != std::string::npos &&
              line.find("\"cpuPercent\":null") != std::string::npos && line.find("\"processes\":[]") != std::string::npos,
              "Incomplete enumeration diagnostics must be retained in the actual sample encoder");
      require(line.find("\"allLifetimeProcessesObserved\":false") != std::string::npos &&
              line.find("\"sharedBrowserAttributionVerified\":false") != std::string::npos,
              "A poll must not claim complete lifetime/shared-browser coverage");
    });
    test([] {
      require(qpc_run(L"--qpc-window=1234567890abcdef1234567890abcdef")=="1234567890abcdef1234567890abcdef","Valid QPC run rejected");
      for(const auto* option:{L"--qpc-window=00000000000000000000000000000000",L"--qpc-window=ABCDEF1234567890abcdef1234567890",L"--qpc-window=a",L"--other=1234567890abcdef1234567890abcdef"}) {
        bool rejected=false;try{(void)qpc_run(option);}catch(const std::exception&){rejected=true;}
        require(rejected,"Malformed explicit QPC run accepted");
      }
    });
    test([] {
      sample before,after;member root;root.pid=11;root.created=100;root.cpu=90;before.members.push_back(root);
      root.cpu=150;after.members.push_back(root);
      qpc_sample prior;prior.begin=10;prior.end=11;prior.system=500;
      const auto value=clock_delta(before,after,prior,20,21,1000,true);
      require(value.stable_cohort&&value.have_previous&&value.cpu_delta==60&&value.system_delta==500&&value.interval_begin==10,
              "Stable adjacent cohort lost its exact CPU interval");
      const auto encoded=encode_sample(after,11,1,value.cpu_delta,value.system_delta,true,&value);
      require(encoded.find("\"captureBeginQpc\":\"20\",\"captureEndQpc\":\"21\"")!=std::string::npos&&
              encoded.find("\"cpuDelta100ns\":\"60\",\"systemDelta100ns\":\"500\",\"cpuDeltaComplete\":true")!=std::string::npos&&
              encoded.find("\"cpuTime100ns\":\"150\"")!=std::string::npos,"QPC raw counters were narrowed or omitted");
    });
    test([] {
      sample before,after;member root;root.pid=11;root.created=100;root.cpu=90;before.members.push_back(root);
      root.cpu=150;after.members.push_back(root);member child;child.pid=22;child.created=101;child.cpu=10;after.members.push_back(child);
      qpc_sample prior;prior.begin=10;prior.end=11;prior.system=500;
      const auto value=clock_delta(before,after,prior,20,21,1000,true);
      const auto encoded=encode_sample(after,11,1,0,value.system_delta,false,&value);
      require(!value.stable_cohort&&encoded.find("\"cpuDelta100ns\":null")!=std::string::npos&&
              encoded.find("\"cpuDeltaComplete\":false")!=std::string::npos,"New process was silently omitted from complete CPU");
      const auto first=clock_delta(sample{},before,qpc_sample{},10,11,500,false);
      const auto first_encoded=encode_sample(before,11,0,0,0,false,&first);
      require(first_encoded.find("\"cpuIntervalBeginQpc\":null,\"cpuIntervalEndQpc\":null,\"cpuDelta100ns\":null,\"systemDelta100ns\":null")!=std::string::npos,
              "First poll fabricated a CPU interval");
    });
    test([] {
      sample before,after;member root;root.pid=11;root.created=100;root.cpu=90;before.members.push_back(root);after.members.push_back(root);
      qpc_sample prior;prior.begin=10;prior.end=11;prior.system=500;
      for(unsigned fault=0;fault<3;++fault) {
        auto current=after;bool rejected=false;
        if(fault==2)current.members[0].cpu=89;
        try{(void)clock_delta(before,current,prior,fault==0?9:20,21,fault==1?499:1000,true);}catch(const std::exception&){rejected=true;}
        require(rejected,"Reversed QPC/system/process counter was accepted");
      }
      qpc_sample failed;failed.begin=20;failed.end=21;failed.counters_available=false;
      const auto encoded=encode_sample(sample{},11,1,0,0,false,&failed);
      require(encoded.find("\"systemTime100ns\":null")!=std::string::npos,"Incomplete enumeration fabricated a system counter");
    });
    test([] {
      struct failed_close {unsigned calls{};bool close(HANDLE){++calls;return false;}} api;
      handle output{reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x1234))};bool rejected=false;
      try{close_output(output,api);}catch(const std::exception&){rejected=true;}
      require(rejected&&api.calls==1&&output.value==nullptr,"A failed final output close was accepted or retried");
    });
    std::cout << "{\"schema\":\"soui.process-tree-enumeration-contracts/v1\",\"passed\":" << passed
              << ",\"guiExecuted\":false,\"processTreeSampled\":false}\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}

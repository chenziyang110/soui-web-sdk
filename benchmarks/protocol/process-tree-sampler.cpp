#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include "trace-identity.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
struct handle {
  HANDLE value{nullptr};
  explicit handle(HANDLE input = nullptr) : value(input) {}
  ~handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  handle(const handle&) = delete;
  handle& operator=(const handle&) = delete;
};

enum class enumeration_failure { none, snapshot, first, next, close };

struct enumeration_status {
  enumeration_failure failure{enumeration_failure::none};
  DWORD error{};
  DWORD close_error{};
  bool close_failed{};
  bool exhausted{};
  bool complete() const noexcept {
    return exhausted && failure == enumeration_failure::none;
  }
};

struct toolhelp_api {
  void clear_error() const noexcept { SetLastError(ERROR_SUCCESS); }
  DWORD last_error() const noexcept { return GetLastError(); }
  HANDLE snapshot() const noexcept { return CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); }
  BOOL first(HANDLE value, PROCESSENTRY32W* entry) const noexcept { return Process32FirstW(value, entry); }
  BOOL next(HANDLE value, PROCESSENTRY32W* entry) const noexcept { return Process32NextW(value, entry); }
  BOOL close(HANDLE value) const noexcept { return CloseHandle(value); }
};

template<class Api>
struct snapshot_owner {
  Api& api;
  HANDLE value;
  enumeration_status& status;
  void close() noexcept {
    if (!value || value == INVALID_HANDLE_VALUE) return;
    const auto held = value;
    value = nullptr; // Exactly one close attempt, including failure/unwind.
    api.clear_error();
    if (!api.close(held)) {
      status.close_error = api.last_error();
      status.close_failed = true;
      if (status.failure == enumeration_failure::none) {
        status.failure = enumeration_failure::close;
        status.error = status.close_error;
      }
    }
  }
  ~snapshot_owner() { close(); }
};

// This is the production enumerator; the contract substitutes only Win32 calls.
// A false API result with error 0 is still a failure, never normal exhaustion.
template<class Api, class Consume>
enumeration_status enumerate_process_snapshot(Api& api, Consume&& consume) {
  enumeration_status status;
  api.clear_error();
  const auto snapshot = api.snapshot();
  if (!snapshot || snapshot == INVALID_HANDLE_VALUE) {
    status.failure = enumeration_failure::snapshot;
    status.error = api.last_error();
    return status;
  }
  snapshot_owner<Api> owner{api, snapshot, status};
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  api.clear_error();
  if (!api.first(snapshot, &entry)) {
    const auto error = api.last_error();
    if (error == ERROR_NO_MORE_FILES) status.exhausted = true;
    else { status.failure = enumeration_failure::first; status.error = error; }
  } else {
    for (;;) {
      consume(entry);
      api.clear_error();
      if (!api.next(snapshot, &entry)) {
        const auto error = api.last_error();
        if (error == ERROR_NO_MORE_FILES) status.exhausted = true;
        else { status.failure = enumeration_failure::next; status.error = error; }
        break;
      }
    }
  }
  // Must precede return: correctness cannot depend on named return elision.
  owner.close();
  return status;
}

const char* enumeration_failure_name(enumeration_failure value) noexcept {
  switch (value) {
    case enumeration_failure::none: return "none";
    case enumeration_failure::snapshot: return "snapshot";
    case enumeration_failure::first: return "first";
    case enumeration_failure::next: return "next";
    case enumeration_failure::close: return "close";
  }
  return "unknown";
}

std::uint64_t ticks(FILETIME value) {
  return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

std::uint64_t process_time(HANDLE process, std::uint64_t& created) {
  FILETIME creation{}, exit{}, kernel{}, user{};
  if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
    throw std::runtime_error("Cannot read process times");
  created = ticks(creation);
  return ticks(kernel) + ticks(user);
}

std::uint64_t system_time() {
  FILETIME idle{}, kernel{}, user{};
  if (!GetSystemTimes(&idle, &kernel, &user))
    throw std::runtime_error("Cannot read system times");
  return ticks(kernel) + ticks(user);
}

void write_line(HANDLE file, const std::string& line) {
  DWORD written = 0;
  if (line.size() > std::numeric_limits<DWORD>::max() ||
      !WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) ||
      written != line.size() || !FlushFileBuffers(file))
    throw std::runtime_error("Cannot write sampler output");
}

struct output_close_api {
  bool close(HANDLE file) const noexcept { return CloseHandle(file)!=FALSE; }
};
template<class Api>
void close_output(handle& file,Api& api) {
  const auto value=file.value;file.value=nullptr; // One attempt, including a failed close.
  if (!api.close(value)) throw std::runtime_error("Cannot close sampler output");
}

unsigned long parse_number(const wchar_t* text, unsigned long maximum) {
  if (!text || !*text) throw std::runtime_error("Missing sampler number");
  std::uint64_t number = 0;
  for (const wchar_t* next = text; *next; ++next) {
    if (*next < L'0' || *next > L'9') throw std::runtime_error("Invalid sampler number");
    const auto digit = static_cast<unsigned>(*next - L'0');
    if (number > (maximum - digit) / 10U) throw std::runtime_error("Sampler number exceeds limit");
    number = number * 10U + digit;
  }
  if (!number) throw std::runtime_error("Sampler number must be positive");
  return static_cast<unsigned long>(number);
}

struct member {
  DWORD pid{};
  DWORD parent{};
  DWORD threads{};
  std::uint64_t created{};
  std::uint64_t cpu{};
  std::uint64_t private_bytes{};
  std::uint64_t working_set_bytes{};
  DWORD handles{};
};

struct sample {
  // Only completeness of this polled, identity-checked observation. This does
  // not certify short-lived processes between polls or shared-browser ownership.
  bool complete{true};
  enumeration_status enumeration;
  std::uint64_t private_bytes{};
  std::uint64_t working_set_bytes{};
  DWORD handles{};
  DWORD threads{};
  std::vector<member> members;
};

// Optional monotonic resource-window evidence. Legacy v2 output is unchanged
// unless the caller explicitly supplies a run-bound --qpc-window option.
struct qpc_sample {
  std::uint64_t begin{}, end{}, system{};
  std::uint64_t interval_begin{}, cpu_delta{}, system_delta{};
  bool have_previous{}, stable_cohort{}, counters_available{true};
};

std::uint64_t qpc_now() {
  LARGE_INTEGER value{};
  if (!QueryPerformanceCounter(&value) || value.QuadPart <= 0)
    throw std::runtime_error("Sampler QPC unavailable");
  return static_cast<std::uint64_t>(value.QuadPart);
}

bool root_exited(HANDLE process) {
  const auto status = WaitForSingleObject(process, 0);
  if (status == WAIT_OBJECT_0) return true;
  if (status != WAIT_TIMEOUT) throw std::runtime_error("Cannot observe root process state");
  return false;
}

std::string qpc_run(const wchar_t* text) {
  const std::wstring option(text ? text : L""), prefix(L"--qpc-window=");
  if (option.rfind(prefix, 0) != 0 || option.size() != prefix.size() + 32)
    throw std::runtime_error("Expected --qpc-window=<32 lowercase hexadecimal run>");
  std::string run;
  bool nonzero = false;
  for (const auto digit : option.substr(prefix.size())) {
    if (!((digit >= L'0' && digit <= L'9') || (digit >= L'a' && digit <= L'f')))
      throw std::runtime_error("Invalid QPC window run identity");
    nonzero = nonzero || digit != L'0';
    run.push_back(static_cast<char>(digit));
  }
  if (!nonzero) throw std::runtime_error("Zero QPC window run identity");
  return run;
}

qpc_sample clock_delta(const sample& previous, const sample& current,
                       const qpc_sample& prior, std::uint64_t begin,
                       std::uint64_t end, std::uint64_t system, bool have_previous) {
  if (!begin || end < begin || (have_previous && begin < prior.end))
    throw std::runtime_error("Sampler QPC reversed");
  qpc_sample result;result.begin=begin;result.end=end;result.system=system;result.have_previous=have_previous;
  if (!have_previous) return result;
  if (system < prior.system) throw std::runtime_error("Sampler system CPU counter reversed");
  result.interval_begin=prior.begin;result.system_delta=system-prior.system;
  result.stable_cohort=previous.complete&&current.complete&&previous.members.size()==current.members.size();
  std::unordered_map<DWORD,const member*> prior_members;
  prior_members.reserve(previous.members.size());
  for (const auto& item : previous.members) prior_members.emplace(item.pid,&item);
  for (const auto& item : current.members) {
    const auto found=prior_members.find(item.pid);
    if (found==prior_members.end()||found->second->created!=item.created) {result.stable_cohort=false;continue;}
    if (item.cpu < found->second->cpu) throw std::runtime_error("Sampler process CPU counter reversed");
    const auto delta=item.cpu-found->second->cpu;
    if (delta > std::numeric_limits<std::uint64_t>::max()-result.cpu_delta)
      throw std::runtime_error("Sampler CPU delta overflow");
    result.cpu_delta+=delta;
  }
  return result;
}

template<class Api>
sample collect(DWORD root_pid, std::uint64_t root_creation,
               const std::unordered_map<DWORD, std::uint64_t>& known, Api& api) {
  std::unordered_map<DWORD, PROCESSENTRY32W> entries;
  std::unordered_multimap<DWORD, DWORD> children;
  sample result;
  result.enumeration = enumerate_process_snapshot(api, [&](const PROCESSENTRY32W& entry) {
    entries.emplace(entry.th32ProcessID, entry);
    children.emplace(entry.th32ParentProcessID, entry.th32ProcessID);
  });
  if (!result.enumeration.complete()) {
    result.complete = false;
    return result; // A partial snapshot must never establish parent membership.
  }

  struct pending_process { DWORD pid; std::uint64_t expected_creation; std::uint64_t parent_creation; };
  std::vector<pending_process> pending{{root_pid, root_creation, 0}};
  for (const auto& previous : known) {
    if (previous.first != root_pid) pending.push_back({previous.first, previous.second, 0});
  }
  std::unordered_set<DWORD> seen;
  for (std::size_t index = 0; index < pending.size(); ++index) {
    const auto [pid, expected_creation, parent_creation] = pending[index];
    if (seen.find(pid) != seen.end()) continue;
    const auto found = entries.find(pid);
    if (found == entries.end()) {
      if (pid == root_pid || !expected_creation) result.complete = false;
      continue;
    }
    handle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid)};
    if (!process.value) {
      if (GetLastError() != ERROR_INVALID_PARAMETER) result.complete = false;
      continue;
    }
    member current;
    current.pid = pid;
    current.parent = found->second.th32ParentProcessID;
    current.threads = found->second.cntThreads;
    try { current.cpu = process_time(process.value, current.created); }
    catch (...) {
      if (WaitForSingleObject(process.value, 0) != WAIT_OBJECT_0) result.complete = false;
      continue;
    }
    if (expected_creation && current.created != expected_creation) {
      if (pid == root_pid) result.complete = false;
      continue;
    }
    // A reused parent PID cannot adopt a process created before this parent.
    if (parent_creation && current.created < parent_creation) continue;
    seen.insert(pid);
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(process.value, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) ||
        !GetProcessHandleCount(process.value, &current.handles)) {
      if (WaitForSingleObject(process.value, 0) != WAIT_OBJECT_0) result.complete = false;
      continue;
    }
    current.private_bytes = memory.PrivateUsage;
    current.working_set_bytes = memory.WorkingSetSize;
    result.private_bytes += current.private_bytes;
    result.working_set_bytes += current.working_set_bytes;
    result.handles += current.handles;
    result.threads += current.threads;
    result.members.push_back(current);
    const auto range = children.equal_range(pid);
    for (auto child = range.first; child != range.second; ++child)
      pending.push_back({child->second, 0, current.created});
  }
  if (result.members.empty() || result.members.front().pid != root_pid) result.complete = false;
  return result;
}

sample collect(DWORD root_pid, std::uint64_t root_creation,
               const std::unordered_map<DWORD, std::uint64_t>& known) {
  toolhelp_api api;
  return collect(root_pid, root_creation, known, api);
}

std::string encode_sample(const sample& value, DWORD root_pid, std::uint64_t elapsed,
                          std::uint64_t cpu_delta, std::uint64_t system_delta, bool have_delta,
                          const qpc_sample* clock = nullptr) {
  std::string line = "{\"event\":\"sample\",\"elapsedMs\":" + std::to_string(elapsed) +
      ",\"rootPid\":" + std::to_string(root_pid) +
      ",\"complete\":" + (value.complete ? "true" : "false") +
      ",\"completeScope\":\"observed-processes-at-poll\",\"allLifetimeProcessesObserved\":false" +
      ",\"sharedBrowserAttributionVerified\":false" +
      ",\"privateBytes\":" + std::to_string(value.private_bytes) +
      ",\"workingSetBytes\":" + std::to_string(value.working_set_bytes) +
      ",\"handles\":" + std::to_string(value.handles) +
      ",\"threads\":" + std::to_string(value.threads) +
      ",\"cpuPercent\":";
  if (have_delta && system_delta) {
    const double percent = 100.0 * static_cast<double>(cpu_delta) / static_cast<double>(system_delta);
    line += std::to_string(percent);
  } else line += "null";
  if (clock) {
    const auto encoded=[](std::uint64_t number){return '"'+std::to_string(number)+'"';};
    line += ",\"captureBeginQpc\":"+encoded(clock->begin)+",\"captureEndQpc\":"+encoded(clock->end)+
        ",\"systemTime100ns\":"+(clock->counters_available?encoded(clock->system):"null")+
        ",\"cpuIntervalBeginQpc\":"+(clock->have_previous?encoded(clock->interval_begin):"null")+
        ",\"cpuIntervalEndQpc\":"+(clock->have_previous?encoded(clock->end):"null")+
        ",\"cpuDelta100ns\":"+(clock->have_previous&&clock->stable_cohort?encoded(clock->cpu_delta):"null")+
        ",\"systemDelta100ns\":"+(clock->have_previous?encoded(clock->system_delta):"null")+
        ",\"cpuDeltaComplete\":"+(clock->have_previous&&clock->stable_cohort?"true":"false");
  }
  line += ",\"enumeration\":{\"complete\":" + std::string(value.enumeration.complete() ? "true" : "false") +
      ",\"failure\":\"" + enumeration_failure_name(value.enumeration.failure) +
      "\",\"win32Error\":" + std::to_string(value.enumeration.error) +
      ",\"closeFailed\":" + (value.enumeration.close_failed ? "true" : "false") +
      ",\"closeError\":" + std::to_string(value.enumeration.close_error) + "},\"processes\":[";
  for (const auto& process : value.members) {
    if (&process != &value.members.front()) line += ',';
    line += "{\"pid\":" + std::to_string(process.pid) +
        ",\"parentPid\":" + std::to_string(process.parent) +
        ",\"creationTime100ns\":" + soui::benchmark::encode_filetime(process.created) +
        ",\"privateBytes\":" + std::to_string(process.private_bytes) +
        ",\"workingSetBytes\":" + std::to_string(process.working_set_bytes) +
        ",\"handles\":" + std::to_string(process.handles) +
        ",\"threads\":" + std::to_string(process.threads);
    if (clock) line += ",\"cpuTime100ns\":\""+std::to_string(process.cpu)+"\"";
    line += "}";
  }
  return line + "]}\n";
}

int run(int argc, wchar_t** argv) {
  if (argc != 5 && argc != 6 && argc != 7) throw std::runtime_error("Usage: ProcessTreeSampler PID INTERVAL_MS DURATION_MS ABSOLUTE_NEW_OUTPUT.ndjson [EXPECTED_CREATION_FILETIME [--qpc-window=RUN]]");
  const auto run_id=argc==7?qpc_run(argv[6]):std::string{};
  const std::optional<std::uint64_t> expected = argc >= 6 ?
      std::optional<std::uint64_t>{soui::benchmark::parse_expected_filetime(argv[5])} : std::nullopt;
  const DWORD root_pid = parse_number(argv[1], std::numeric_limits<DWORD>::max());
  const DWORD interval = parse_number(argv[2], 60000);
  const DWORD duration = parse_number(argv[3], 24U * 60U * 60U * 1000U);
  const std::filesystem::path output(argv[4]);
  if (!output.is_absolute()) throw std::runtime_error("An absolute output path is required");
  handle root{OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, root_pid)};
  if (!root.value) throw std::runtime_error("Root process is unavailable");
  std::uint64_t root_creation = 0;
  (void)process_time(root.value, root_creation);
  if (expected && root_creation != *expected) throw std::runtime_error("Root creation identity differs from expected FILETIME");
  LARGE_INTEGER frequency{};
  std::uint64_t collector_creation{}, run_begin{};
  if (!run_id.empty()) {
    if (root_pid==GetCurrentProcessId()||!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)
      throw std::runtime_error("Invalid QPC sampler collector/frequency");
    (void)process_time(GetCurrentProcess(),collector_creation);run_begin=qpc_now();
  }
  handle file{CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create new sampler output");
  FILETIME utc{};
  GetSystemTimeAsFileTime(&utc);
  auto begin_line=soui::benchmark::process_begin(root_pid, root_creation, ticks(utc), interval, duration, expected);
  if (!run_id.empty()) {
    begin_line.resize(begin_line.size()-2);
    begin_line+=",\"clockCapability\":\"qpc-interval-v1\",\"run_id\":\""+run_id+"\",\"qpcFrequency\":\""+
      std::to_string(frequency.QuadPart)+"\",\"runBeginQpc\":\""+std::to_string(run_begin)+
      "\",\"collectorPid\":"+std::to_string(GetCurrentProcessId())+",\"collectorCreationFileTime\":\""+
      std::to_string(collector_creation)+"\"}\n";
  }
  write_line(file.value, begin_line);
  const auto start = GetTickCount64();
  std::unordered_map<DWORD, std::pair<std::uint64_t, std::uint64_t>> previous_cpu;
  std::unordered_map<DWORD, std::uint64_t> known;
  std::uint64_t previous_system = 0;
  unsigned incomplete = 0;
  unsigned samples = 0;
  bool expired = false;
  sample previous_clock_sample;qpc_sample previous_clock;bool have_clock_sample=false;
  for (;;) {
    if (root_exited(root.value)) break;
    const auto capture_begin=run_id.empty()?0:qpc_now();
    auto current = collect(root_pid, root_creation, known);
    if (!current.enumeration.complete()) {
      ++incomplete;
      qpc_sample failed_clock;failed_clock.counters_available=false;
      if (!run_id.empty()) {
        failed_clock.begin=capture_begin;failed_clock.end=qpc_now();
        if (failed_clock.end<capture_begin||(have_clock_sample&&capture_begin<previous_clock.end))
          throw std::runtime_error("Sampler QPC reversed during incomplete enumeration");
      }
      write_line(file.value, encode_sample(current, root_pid, GetTickCount64() - start, 0, 0, false,run_id.empty()?nullptr:&failed_clock));
      ++samples;
      break; // Do not retry or hide this failure if the root concurrently exits.
    }
    if ((current.members.empty() || current.members.front().pid != root_pid) &&
        root_exited(root.value)) break;
    if (!run_id.empty()&&std::any_of(current.members.begin(),current.members.end(),[](const member& item){return item.pid==GetCurrentProcessId();}))
      current.complete=false; // Never count the sampler inside the measured application tree.
    if (!current.complete) ++incomplete;
    const auto system = system_time();
    std::uint64_t delta = 0;
    known.clear();
    for (const auto& member : current.members) {
      const auto prior = previous_cpu.find(member.pid);
      if (prior != previous_cpu.end() && prior->second.first == member.created &&
          member.cpu >= prior->second.second) delta += member.cpu - prior->second.second;
      previous_cpu[member.pid] = {member.created, member.cpu};
      known[member.pid] = member.created;
    }
    const auto clock=run_id.empty()?qpc_sample{}:clock_delta(previous_clock_sample,current,previous_clock,
      capture_begin,qpc_now(),system,have_clock_sample);
    write_line(file.value, encode_sample(current, root_pid, GetTickCount64() - start,
        run_id.empty()?delta:clock.cpu_delta,run_id.empty()?(system>=previous_system?system-previous_system:0):clock.system_delta,
        run_id.empty()?previous_system!=0:clock.have_previous&&clock.stable_cohort,run_id.empty()?nullptr:&clock));
    ++samples;
    previous_system = system;
    if (!run_id.empty()) {previous_clock_sample=std::move(current);previous_clock=clock;have_clock_sample=true;}
    if (root_exited(root.value)) break;
    if (GetTickCount64() - start >= duration) { expired = true; break; }
    Sleep(interval);
  }
  DWORD exit_code = STILL_ACTIVE;
  if (!GetExitCodeProcess(root.value, &exit_code)) throw std::runtime_error("Cannot read root process exit code");
  const auto run_end=run_id.empty()?0:qpc_now();
  if (!run_id.empty()&&(run_end<run_begin||(have_clock_sample&&run_end<previous_clock.end)))
    throw std::runtime_error("Sampler terminal QPC reversed");
  write_line(file.value, "{\"event\":\"run.end\",\"elapsedMs\":" +
      std::to_string(GetTickCount64() - start) + ",\"rootExitCode\":" + std::to_string(exit_code) +
      ",\"durationExpired\":" + (expired ? "true" : "false") +
      ",\"samples\":" + std::to_string(samples) +
      ",\"incompleteSamples\":" + std::to_string(incomplete) +
      (run_id.empty()?std::string{}:",\"runEndQpc\":\""+std::to_string(run_end)+"\"")+"}\n");
  output_close_api close_api;close_output(file,close_api);
  if (expired) return 2;
  return (incomplete || !samples) ? 3 : 0;
}
}  // namespace

#ifndef SOUI_PROCESS_TREE_SAMPLER_CONTRACT
int wmain(int argc, wchar_t** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
#endif

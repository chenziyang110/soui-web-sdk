#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include "trace-identity.h"

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
  bool complete{true};
  std::uint64_t private_bytes{};
  std::uint64_t working_set_bytes{};
  DWORD handles{};
  DWORD threads{};
  std::vector<member> members;
};

sample collect(DWORD root_pid, std::uint64_t root_creation,
               const std::unordered_map<DWORD, std::uint64_t>& known) {
  handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
  if (snapshot.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate process tree");
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot.value, &entry)) throw std::runtime_error("Cannot read process tree");
  std::unordered_map<DWORD, PROCESSENTRY32W> entries;
  std::unordered_multimap<DWORD, DWORD> children;
  do {
    entries.emplace(entry.th32ProcessID, entry);
    children.emplace(entry.th32ParentProcessID, entry.th32ProcessID);
  } while (Process32NextW(snapshot.value, &entry));

  sample result;
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

std::string encode_sample(const sample& value, DWORD root_pid, std::uint64_t elapsed,
                          std::uint64_t cpu_delta, std::uint64_t system_delta, bool have_delta) {
  std::string line = "{\"event\":\"sample\",\"elapsedMs\":" + std::to_string(elapsed) +
      ",\"rootPid\":" + std::to_string(root_pid) +
      ",\"complete\":" + (value.complete ? "true" : "false") +
      ",\"privateBytes\":" + std::to_string(value.private_bytes) +
      ",\"workingSetBytes\":" + std::to_string(value.working_set_bytes) +
      ",\"handles\":" + std::to_string(value.handles) +
      ",\"threads\":" + std::to_string(value.threads) +
      ",\"cpuPercent\":";
  if (have_delta && system_delta) {
    const double percent = 100.0 * static_cast<double>(cpu_delta) / static_cast<double>(system_delta);
    line += std::to_string(percent);
  } else line += "null";
  line += ",\"processes\":[";
  for (const auto& process : value.members) {
    if (&process != &value.members.front()) line += ',';
    line += "{\"pid\":" + std::to_string(process.pid) +
        ",\"parentPid\":" + std::to_string(process.parent) +
        ",\"creationTime100ns\":" + soui::benchmark::encode_filetime(process.created) +
        ",\"privateBytes\":" + std::to_string(process.private_bytes) +
        ",\"workingSetBytes\":" + std::to_string(process.working_set_bytes) +
        ",\"handles\":" + std::to_string(process.handles) +
        ",\"threads\":" + std::to_string(process.threads) + "}";
  }
  return line + "]}\n";
}

int run(int argc, wchar_t** argv) {
  if (argc != 5 && argc != 6) throw std::runtime_error("Usage: ProcessTreeSampler PID INTERVAL_MS DURATION_MS ABSOLUTE_NEW_OUTPUT.ndjson [EXPECTED_CREATION_FILETIME]");
  const std::optional<std::uint64_t> expected = argc == 6 ?
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
  handle file{CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create new sampler output");
  FILETIME utc{};
  GetSystemTimeAsFileTime(&utc);
  write_line(file.value, soui::benchmark::process_begin(root_pid, root_creation, ticks(utc), interval, duration, expected));
  const auto start = GetTickCount64();
  std::unordered_map<DWORD, std::pair<std::uint64_t, std::uint64_t>> previous_cpu;
  std::unordered_map<DWORD, std::uint64_t> known;
  std::uint64_t previous_system = 0;
  unsigned incomplete = 0;
  unsigned samples = 0;
  bool expired = false;
  for (;;) {
    if (WaitForSingleObject(root.value, 0) == WAIT_OBJECT_0) break;
    const auto current = collect(root_pid, root_creation, known);
    if ((current.members.empty() || current.members.front().pid != root_pid) &&
        WaitForSingleObject(root.value, 0) == WAIT_OBJECT_0) break;
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
    write_line(file.value, encode_sample(current, root_pid, GetTickCount64() - start, delta,
        system >= previous_system ? system - previous_system : 0, previous_system != 0));
    ++samples;
    previous_system = system;
    if (WaitForSingleObject(root.value, 0) == WAIT_OBJECT_0) break;
    if (GetTickCount64() - start >= duration) { expired = true; break; }
    Sleep(interval);
  }
  DWORD exit_code = STILL_ACTIVE;
  (void)GetExitCodeProcess(root.value, &exit_code);
  write_line(file.value, "{\"event\":\"run.end\",\"elapsedMs\":" +
      std::to_string(GetTickCount64() - start) + ",\"rootExitCode\":" + std::to_string(exit_code) +
      ",\"durationExpired\":" + (expired ? "true" : "false") +
      ",\"samples\":" + std::to_string(samples) +
      ",\"incompleteSamples\":" + std::to_string(incomplete) + "}\n");
  if (expired) return 2;
  return (incomplete || !samples) ? 3 : 0;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}

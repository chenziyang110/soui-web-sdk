#include "../app/reference-agent.h"
#include "../app/reference-business.h"
#include "public-endpoint-client.h"
#include <json.hpp>
#include <soui/runtime/bridge/action_contract.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using json = nlohmann::json;
using public_endpoint_tests::client;
using namespace soui::runtime::agent;
json child_receipts = json::array(); // Owner-only actual post-exit observations.
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
[[noreturn]] void pending_failure() noexcept { TerminateProcess(GetCurrentProcess(), 91); ExitProcess(91); }
class handle final {
 public:
  explicit handle(HANDLE native = nullptr) noexcept : native_{native} {}
  ~handle() { reset(); }
  handle(const handle&) = delete; handle& operator=(const handle&) = delete;
  HANDLE get() const noexcept { return native_; }
  HANDLE release() noexcept { const auto result = native_; native_ = nullptr; return result; }
  void reset(HANDLE value = nullptr) noexcept { if (native_ && native_ != INVALID_HANDLE_VALUE) CloseHandle(native_); native_ = value; }
 private: HANDLE native_;
};
std::uint64_t creation(HANDLE process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  require(GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE, "Native creation unavailable");
  return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) | created.dwLowDateTime;
}
std::string component() { return "soui-reference-agent-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()); }
std::wstring wide(const std::string& value) { return std::wstring(value.begin(), value.end()); } // Only bounded ASCII arguments.
class child_process final {
 public:
  child_process(const std::string& name, const std::string& pipe, const std::string& page_task = "none") : mode_{name} {
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    handle read, write;
    HANDLE native_read{}, native_write{};
    require(CreatePipe(&native_read, &native_write, &attributes, 32768) != FALSE, "Child receipt pipe failed");
    read.reset(native_read); write.reset(native_write);
    require(SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0) != FALSE, "Receipt inheritance failed");
    HANDLE native_input{}, native_control{};
    require(CreatePipe(&native_input, &native_control, &attributes, 4096), "Child input pipe failed");
    handle input{native_input}; input_.reset(native_control);
    require(SetHandleInformation(input_.get(), HANDLE_FLAG_INHERIT, 0), "Input inheritance failed");
    ready_.reset(CreateEventW(&attributes, TRUE, FALSE, nullptr));
    release_.reset(CreateEventW(&attributes, TRUE, FALSE, nullptr));
    require(ready_.get() && release_.get(), "Native child gate failed");
    std::array<wchar_t, 32768> buffer{};
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    require(length && length < buffer.size(), "Actual test path failed");
    const std::wstring executable{buffer.data(), length};
    std::wstring command = L"\"" + executable + L"\" --child " + wide(name) + L" " + wide(pipe) + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(ready_.get())) + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(release_.get())) + L" " + wide(page_task);
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = input.get(); startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = write.get();
    SIZE_T bytes{};
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    require(bytes && bytes < 65536, "Attribute list sizing failed");
    std::vector<unsigned char> storage(bytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    require(InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes) != FALSE, "Attribute initialization failed");
    struct list_exit { LPPROC_THREAD_ATTRIBUTE_LIST value; ~list_exit() { DeleteProcThreadAttributeList(value); } } finish{startup.lpAttributeList};
    HANDLE inherited[]{input.get(), write.get(), ready_.get(), release_.get()};
    require(UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr) != FALSE, "Explicit inherited handles failed");
    PROCESS_INFORMATION process{};
    require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
        &startup.StartupInfo, &process) != FALSE, "Actual child creation failed");
    process_.reset(process.hProcess); thread_.reset(process.hThread); id_ = process.dwProcessId;
    // Any post-create failure is fail-stop: the Root Job retains the actual child.
    try { creation_ = creation(process_.get()); }
    catch (...) { pending_failure(); }
    BOOL in_job{};
    if (!IsProcessInJob(process_.get(), nullptr, &in_job) || !in_job || !id_ || !creation_) pending_failure();
    receipt_.reset(read.release());
  }
  ~child_process() { if (!completed()) pending_failure(); }
  child_process(const child_process&) = delete; child_process& operator=(const child_process&) = delete;
  DWORD id() const noexcept { return id_; }
  std::uint64_t created() const noexcept { return creation_; }
  bool completed() const noexcept { return WaitForSingleObject(process_.get(), 0) == WAIT_OBJECT_0; }
  bool signalled() const noexcept { return WaitForSingleObject(ready_.get(), 0) == WAIT_OBJECT_0; }
  void resume() { require(ResumeThread(thread_.get()) != static_cast<DWORD>(-1), "Child resume failed"); thread_.reset(); }
  void release() { require(SetEvent(release_.get()) != FALSE, "Child release failed"); }
  void set_page_task(const std::string& task) {
    require(!task.empty() && task.size() <= 128 && task.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.:") == std::string::npos, "Task correlation invalid");
    const auto body = task + '\n'; DWORD count{};
    require(WriteFile(input_.get(), body.data(), static_cast<DWORD>(body.size()), &count, nullptr) && count == body.size(), "Actual task correlation handoff failed");
    input_.reset();
  }
  json result() {
    require(completed(), "Child is still live"); DWORD code = STILL_ACTIVE;
    require(GetExitCodeProcess(process_.get(), &code) && code == 0, "Actual child failed");
    std::string raw;
    for (;;) {
      char bytes[4096]{}; DWORD available{}, count{};
      if (!PeekNamedPipe(receipt_.get(), nullptr, 0, nullptr, &available, nullptr)) {
        require(GetLastError() == ERROR_BROKEN_PIPE, "Child receipt availability failed"); break;
      }
      if (!available) break;
      require(raw.size() + available <= 8192, "Child receipt exceeded bound");
      require(ReadFile(receipt_.get(), bytes, std::min<DWORD>(available, sizeof(bytes)), &count, nullptr) && count, "Child receipt read failed");
      raw.append(bytes, count);
    }
    const auto value = json::parse(raw);
    require(value.at("pid") == std::to_string(id_) && value.at("creationFileTime") == std::to_string(creation_), "Child native identity mismatch");
    child_receipts.push_back(json{{"mode", mode_}, {"pid", std::to_string(id_)}, {"creationFileTime", std::to_string(creation_)},
      {"exitCode", code}, {"jobAssigned", true}, {"rawReceipt", raw}});
    return value;
  }
 private:
  handle process_, thread_, receipt_, ready_, release_, input_;
  DWORD id_{}; std::uint64_t creation_{};
  const std::string mode_;
};
struct environment final {
  const DWORD owner{GetCurrentThreadId()};
  const std::string name{component()};
  HWND window{};
  bool native_window_current{true};
  std::shared_ptr<reference::business> business;
  std::unique_ptr<soui::sdk::json_bridge> bridge;
  std::unique_ptr<reference::agent> agent;
  windows_local_endpoint* default_endpoint{};
  unsigned sequence{};
  explicit environment(const char* fixture) : business{std::make_shared<reference::business>(reference::read_fixture(fixture))},
      bridge{std::make_unique<soui::sdk::json_bridge>("app://reference-workbench")} {
    require(reference::register_business(*bridge, business), "Actual business registration failed");
    window = CreateWindowExW(0, L"STATIC", L"Reference agent native fixture", 0, 0, 0, 1, 1, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(window != nullptr, "Actual owner window failed");
  }
  ~environment() {
    stop();
    if (agent) agent->set_owner_window(nullptr);
    bridge.reset(); // Actual SDK joins before policy/held process/business release.
    agent.reset(); business.reset();
    if (window && !DestroyWindow(window)) pending_failure();
  }
  void configure(DWORD selected = 0) {
    agent = std::make_unique<reference::agent>(*bridge, selected ? reference::agent_options{true, name, selected} : reference::agent_options{});
    agent->set_owner_window(window);
  }
  void start() {
    require(agent && agent->start_bound_document([this] {
      require(GetCurrentThreadId() == owner, "Current-window check escaped owner");
      return native_window_current && IsWindow(window);
    }), "Native-only window binding failed");
  }
  void pump() {
    if (agent) agent->pump_owner();
    if (default_endpoint) (void)default_endpoint->pump_owner();
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
  }
  template<class Predicate> void until(Predicate done, const char* message) {
    const auto deadline = GetTickCount64() + 8000;
    while (!done()) { require(GetTickCount64() < deadline, message); pump(); (void)MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT); }
  }
  void stop() noexcept {
    const auto deadline = GetTickCount64() + 5000;
    if (agent) while (agent->stop_owner() != local_endpoint_stop::stopped) {
      if (GetTickCount64() >= deadline) pending_failure();
      pump(); (void)MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    }
    if (default_endpoint) {
      default_endpoint->request_stop();
      while (default_endpoint->poll_stop() != local_endpoint_stop::stopped) {
        if (GetTickCount64() >= deadline) pending_failure(); pump(); Sleep(1);
      }
    }
  }
  json page(const json& auth, const char* method, json input = json::object()) {
    return json::parse(bridge->handle_json(json{{"requestId", "page-" + std::to_string(++sequence)},
      {"sessionId", auth.value("sessionId", "")}, {"origin", auth.value("origin", "")}, {"token", auth.value("token", "")},
      {"method", method}, {"params", std::move(input)}}.dump()));
  }
  json bootstrap() { const auto value = page(json::object(), "bridge.bootstrap", {{"protocol", "soui.bridge/1"}}); require(value.at("ok") == true, "Page bootstrap failed"); return value.at("result"); }
  std::string page_start(const json& auth, const char* operation, unsigned duration = 500) {
    const auto value = page(auth, "task.start", {{"kind", "reference.summary"}, {"operationId", operation},
      {"input", {{"fixtureId", business->data->id}, {"category", "all"}, {"durationMs", duration}}}});
    require(value.at("ok") == true, "Page summary admission failed"); return value.at("result").at("taskId");
  }
  json page_terminal(const json& auth, const std::string& task) {
    json value;
    until([&] { value = page(auth, "task.query", {{"taskId", task}}); require(value.at("ok") == true, "Page query failed");
      const auto state = value.at("result").value("state", ""); return state == "succeeded" || state == "cancelled" || state == "failed"; }, "Page worker did not finish");
    return value.at("result");
  }
};
json summary_input(unsigned duration = 500) { return {{"fixtureId", "electron-smoothness-10k-v1"}, {"category", "all"}, {"durationMs", duration}}; }
std::string local_start(client& pipe, const char* operation, unsigned duration = 500) {
  const auto result = pipe.call("reference.summary.local.start", {{"operationId", operation}, {"input", summary_input(duration)}});
  require(result.at("ok") == true && result.at("result").at("accepted") == true, "Local admission failed"); return result.at("result").at("taskId");
}
json local_terminal(client& pipe, const std::string& task) {
  const auto deadline = GetTickCount64() + 6000;
  for (;;) {
    const auto response = pipe.call("reference.summary.local.query", {{"taskId", task}});
    require(response.at("ok") == true, "Local terminal query failed"); const auto value = response.at("result");
    const auto state = value.value("state", "");
    if (state == "succeeded" || state == "cancelled" || state == "failed") return value;
    require(GetTickCount64() < deadline, "Local terminal deadline"); Sleep(1);
  }
}
void running(client& pipe, const std::string& task) {
  const auto deadline = GetTickCount64() + 4000;
  for (;;) {
    const auto response = pipe.call("reference.summary.local.query", {{"taskId", task}});
    require(response.at("ok") == true, "Running query failed");
    if (response.at("result").at("state") == "running" && response.at("result").at("progress").get<unsigned>() > 0) return;
    require(GetTickCount64() < deadline, "Actual business progress never entered"); Sleep(1);
  }
}
void deny(const json& value, const char* code = "E_UNAUTHORIZED") { require(value.at("ok") == false && value.at("error").at("code") == code, "Expected native denial missing"); }
json child_case(const std::string& name, const std::string& component, HANDLE ready, HANDLE release, const std::string& page_task) {
  client pipe{component};
  if (name == "denied-peer") {
    require(pipe.write_raw(R"({"kind":"hello","schemaVersion":"soui.local/1"})") && pipe.read_raw().empty(), "Unapproved same-logon peer was accepted"); return json::object();
  }
  pipe.hello();
  if (name == "describe") {
    const auto value = pipe.call("reference.fixture.describe", json::object()); require(value.at("ok") == true && value.at("result").at("rowCount") == 10000, "Typed description failed"); return value.at("result");
  }
  if (name == "fixed-summary") {
    const auto task = local_start(pipe, "local-fixed"); const auto terminal = local_terminal(pipe, task);
    require(terminal.at("state") == "succeeded" && terminal.at("result").size() == 5 && !terminal.contains("message") && !terminal.contains("projectionError"), "Actual safe result missing");
    return {{"taskId", task}, {"result", terminal.at("result")}};
  }
  if (name == "cancel") {
    const auto task = local_start(pipe, "local-cancel", 60000); running(pipe, task);
    const auto cancel = pipe.call("reference.summary.local.cancel", {{"taskId", task}});
    require(cancel.at("ok") == true && cancel.at("result").at("cancelRequested") == true, "Cancel admission failed");
    const auto terminal = local_terminal(pipe, task);
    require(terminal.at("state") == "cancelled" && terminal.at("result") == json::object() && !terminal.contains("projectionError"), "Cancellation was not actual safe terminal"); return {{"taskId", task}};
  }
  if (name == "invalid-form") {
    const auto good = pipe.call("reference.form.submit", {{"name", "中文业务"}, {"notes", "有界草稿"}, {"category", "素材"}});
    require(good.at("ok") == true && good.at("result").at("accepted") == true, "Typed Chinese form failed");
    const auto blank = pipe.call("reference.form.submit", {{"name", " "}, {"notes", ""}, {"category", "素材"}});
    require(blank.at("ok") == true && blank.at("result").at("accepted") == false, "Blank business name accepted");
    deny(pipe.call("reference.form.submit", {{"name", "中文业务"}, {"notes", ""}, {"category", "素材"}, {"userGesture", true}}), "E_INVALID_PARAMS");
    deny(pipe.call("reference.summary.local.start", {{"operationId", "bad-duration"}, {"input", summary_input(0)}}), "E_INVALID_PARAMS");
    return good.at("result");
  }
  if (name == "revoked-document" || name == "close-pending") {
    const auto task = local_start(pipe, "local-before-close", 60000); running(pipe, task);
    if (name == "close-pending") require(pipe.write_length(500), "Actual partial read header failed");
    require(SetEvent(ready) && WaitForSingleObject(release, 5000) == WAIT_OBJECT_0, "Owner close gate failed");
    if (name == "revoked-document") (void)pipe.write_raw(R"({"kind":"request","schemaVersion":"soui.bridge/1","requestId":"late","method":"reference.fixture.describe","params":{}})");
    require(pipe.read_raw().empty(), "Revoked/pending connection delivered result"); return {{"taskId", task}};
  }
  if (name == "page-isolation") {
    deny(pipe.call("task.start", {{"kind", "reference.summary"}, {"operationId", "forbidden"}, {"input", summary_input()}}));
    deny(pipe.call("bridge.probe", json::object()));
    deny(pipe.call("reference.summary.local.query", {{"taskId", page_task}}), "E_TASK_NOT_FOUND");
    const auto discovery = pipe.call("bridge.describe", {{"cursor", 0}, {"limit", 32}});
    require(discovery.at("ok") == true, "Local discovery failed");
    for (const auto& method : discovery.at("result").at("methods")) {
      const auto id = method.at("name").get<std::string>();
      require(id == "reference.fixture.describe" || id == "reference.form.submit" || id == "reference.summary.local.start" || id == "reference.summary.local.query" || id == "reference.summary.local.cancel", "Discovery exposed unauthorized method");
    }
    require(pipe.write_raw(R"({"kind":"request","schemaVersion":"soui.bridge/1","requestId":"forged","method":"reference.fixture.describe","params":{},"token":"page-forged","sessionId":"page-forged","origin":"app://reference-workbench"})") && pipe.read_raw().empty(), "Wire page credential accepted");
    return json::object();
  }
  throw std::runtime_error("Unknown child case");
}
void disabled_default_deny(const char* fixture) {
  wchar_t executable[]{L"reference"}, pipe_option[]{L"--agent-pipe"}, pipe_value[]{L"safe"}, process_option[]{L"--agent-allow-process"}, bad_process[]{L"0"}, smoke[]{L"--smoke-test"};
  wchar_t* normal[]{executable}; require(reference::parse_command_line(1, normal).valid && !reference::parse_command_line(1, normal).agent.enabled, "Default CLI enabled agent");
  wchar_t* missing[]{executable, pipe_option, pipe_value}; require(!reference::parse_command_line(3, missing).valid, "Missing selection accepted");
  wchar_t* zero[]{executable, pipe_option, pipe_value, process_option, bad_process}; require(!reference::parse_command_line(5, zero).valid, "Zero selection accepted");
  wchar_t* mixed[]{executable, smoke, pipe_option, pipe_value}; require(!reference::parse_command_line(4, mixed).valid, "Smoke implicitly allowed agent");
  environment app{fixture}; app.configure();
  require(!app.agent->enabled() && !app.agent->start_bound_document([] { return true; }), "Disabled agent listened");
  handle absent{CreateFileW(public_endpoint_tests::pipe_name(app.name).c_str(), FILE_READ_DATA | FILE_WRITE_DATA, 0, nullptr, OPEN_EXISTING, 0, nullptr)};
  require(absent.get() == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "Default produced a pipe");
  app.default_endpoint = app.bridge->create_local_endpoint();
  require(app.default_endpoint && app.default_endpoint->state() == local_endpoint_state::stopped, "Stopped public factory failed");
  require(app.default_endpoint->start(local_endpoint_options{app.name}) == local_endpoint_error::none, "Default-deny fixture start failed");
  child_process denied{"denied-peer", app.name}; denied.resume(); app.until([&] { return denied.completed(); }, "Default-deny child timeout"); (void)denied.result();
}
// Failure-only fixture observation, before a held child's fail-stop destructor.
// No exception text, page credentials, token data or successful-case output.
void phase_failure(const char* phase, const child_process& selected) noexcept {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return;
  const auto root_creation = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) | created.dwLowDateTime;
  if (!root_creation || !selected.id() || !selected.created()) return;
  char bytes[768]{};
  const int length = std::snprintf(bytes, sizeof(bytes),
      "{\"schema\":\"soui.reference-agent-phase-diagnostic/1\",\"case\":\"selected-peer\",\"phase\":\"%s\","
      "\"pid\":\"%lu\",\"creationFileTime\":\"%llu\",\"selectedPid\":\"%lu\",\"selectedCreationFileTime\":\"%llu\"}\n",
      phase, static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long long>(root_creation),
      static_cast<unsigned long>(selected.id()), static_cast<unsigned long long>(selected.created()));
  if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(bytes)) return;
  DWORD written{};
  (void)WriteFile(GetStdHandle(STD_ERROR_HANDLE), bytes, static_cast<DWORD>(length), &written, nullptr);
}
// Real normal-SDK contract parsing: old projection is valid alone, but exceeds
// the unchanged depth8 after a local query result wrapper. The new map fits.
void local_contract_regression() {
  static_assert(soui::runtime::action_contract::max_depth == 8, "No SDK depth relaxation");
  const auto original = json::parse(R"projection({"type":"object","properties":{"fixtureId":{"type":"string","enum":["electron-smoothness-10k-v1"],"maxLength":64},"processed":{"type":"integer","minimum":10000,"maximum":10000},"matched":{"type":"integer","minimum":0,"maximum":10000},"totalSizeKb":{"type":"integer","minimum":0,"maximum":41110000},"groups":{"type":"array","minItems":5,"maxItems":5,"items":{"type":"object","properties":{"group":{"type":"string","enum":["素材","项目","缓存","导出","归档"],"maxLength":32},"count":{"type":"integer","minimum":0,"maximum":10000}},"required":["group","count"],"additionalProperties":false}}},"additionalProperties":false})projection");
  const auto safe = json::parse(R"projection({"type":"object","properties":{"fixtureId":{"type":"string","enum":["electron-smoothness-10k-v1"],"maxLength":64},"processed":{"type":"integer","minimum":10000,"maximum":10000},"matched":{"type":"integer","minimum":0,"maximum":10000},"totalSizeKb":{"type":"integer","minimum":0,"maximum":41110000},"groups":{"type":"object","properties":{"素材":{"type":"integer","minimum":0,"maximum":10000},"项目":{"type":"integer","minimum":0,"maximum":10000},"缓存":{"type":"integer","minimum":0,"maximum":10000},"导出":{"type":"integer","minimum":0,"maximum":10000},"归档":{"type":"integer","minimum":0,"maximum":10000}},"required":["素材","项目","缓存","导出","归档"],"additionalProperties":false}},"additionalProperties":false})projection");
  const auto descriptor = [](const json& output) {
    return json{{"protocol", "soui.action/1"}, {"title", "Reference local projection boundary"}, {"description", ""},
      {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
      {"outputSchema", output}}.dump();
  };
  const auto query_output = [](const json& projection) {
    return json{{"type", "object"}, {"properties", {{"result", projection}}},
      {"required", {"result"}}, {"additionalProperties", false}};
  };
  require(soui::runtime::action_contract::parse(descriptor(original)) != nullptr, "Original standalone schema boundary changed");
  require(soui::runtime::action_contract::parse(descriptor(query_output(original))) == nullptr, "Deep query projection was accepted");
  const auto contract = soui::runtime::action_contract::parse(descriptor(query_output(safe)));
  require(contract != nullptr, "Fixed local query projection was rejected");
  const json good{{"result", {{"fixtureId", "electron-smoothness-10k-v1"}, {"processed", 10000},
    {"matched", 10000}, {"totalSizeKb", 0}, {"groups", {{"素材", 0}, {"项目", 10000}, {"缓存", 2}, {"导出", 3}, {"归档", 4}}}}}};
  require(contract->validates_output(good.dump()), "Bounded fixed-group output rejected");
  auto invalid = good; invalid["result"]["groups"] = json::array();
  require(!contract->validates_output(invalid.dump()), "Old array projection remained accepted");
  invalid = good; invalid["result"]["groups"].erase("素材");
  require(!contract->validates_output(invalid.dump()), "Missing group accepted");
  invalid = good; invalid["result"]["groups"]["未知"] = 1;
  require(!contract->validates_output(invalid.dump()), "Unknown group accepted");
  invalid = good; invalid["result"]["groups"]["素材"] = -1;
  require(!contract->validates_output(invalid.dump()), "Negative group count accepted");
  invalid = good; invalid["result"]["groups"]["素材"] = 10001;
  require(!contract->validates_output(invalid.dump()), "Oversized group count accepted");
  invalid = good; invalid["result"]["groups"]["素材"] = "1";
  require(!contract->validates_output(invalid.dump()), "Text group count accepted");
  invalid = good; invalid["result"]["groups"]["素材"] = 1.5;
  require(!contract->validates_output(invalid.dump()), "Fractional group count accepted");
}
json page_summary_projection(const json& page) {
  require(page.is_object() && page.size() == 5, "Page summary field shape changed");
  const auto& original = page.at("groups");
  const std::array<std::string, 5> names{"素材", "项目", "缓存", "导出", "归档"};
  require(original.is_array() && original.size() == names.size(), "Page group array shape changed");
  auto groups = json::object();
  for (const auto& item : original) {
    require(item.is_object() && item.size() == 2 && item.contains("group") && item.contains("count") &&
      item.at("group").is_string() && item.at("count").is_number_integer(), "Page group field shape changed");
    const auto name = item.at("group").get<std::string>(); const auto& count = item.at("count");
    const bool bounded = count.is_number_unsigned() ? count.get<std::uint64_t>() <= 10000 :
      count.get<std::int64_t>() >= 0 && count.get<std::int64_t>() <= 10000;
    require(std::find(names.begin(), names.end(), name) != names.end() && !groups.contains(name) && bounded,
      "Page groups were not five unique bounded fixed groups");
    groups[name] = count;
  }
  auto projected = page; projected["groups"] = std::move(groups); return projected;
}
void selected_peer(const char* fixture) {
  environment app{fixture}; child_process selected{"describe", app.name};
  const char* phase = "local-contract-regression";
  try {
  local_contract_regression();
  phase = "configure";
  app.configure(selected.id());
  phase = "bootstrap";
  const auto prior = app.bootstrap();
  phase = "prebind-shutdown";
  std::thread prebind{[&] { app.agent->shutdown_session(prior.at("sessionId").get<std::string>()); }}; prebind.join();
  if (app.agent->closed()) {
    std::cerr << json{{"schema", "soui.reference-agent-prebind-red-observation/1"}, {"case", "selected-peer"},
      {"phase", "after-real-prebind-shutdown"}, {"pid", std::to_string(GetCurrentProcessId())},
      {"creationFileTime", std::to_string(creation(GetCurrentProcess()))}, {"selectedPid", std::to_string(selected.id())},
      {"selectedCreationFileTime", std::to_string(selected.created())}, {"selectedAlive", !selected.completed()},
      {"closed", app.agent->closed()}, {"started", app.agent->started()},
      {"qualificationPassed", false}, {"releaseReady", false}}.dump() << std::endl;
  }
  phase = "prebind-assert";
  require(!app.agent->closed() && !app.agent->started(), "Prebind session cleanup disabled first admission");
  phase = "old-page-denial";
  deny(app.page(prior, "reference.fixture.describe"));
  phase = "new-bootstrap";
  const auto current = app.bootstrap();
  phase = "new-page-probe";
  require(app.page(current, "reference.fixture.describe").at("ok") == true, "Actual new page session was not usable");
  phase = "selected-cli";
  wchar_t executable[]{L"reference"}, pipe_option[]{L"--agent-pipe"}, pipe_value[]{L"safe"}, process_option[]{L"--agent-allow-process"};
  auto selected_text = std::to_wstring(selected.id());
  wchar_t* selected_cli[]{executable, process_option, selected_text.data(), pipe_option, pipe_value};
  const auto parsed = reference::parse_command_line(5, selected_cli);
  require(parsed.valid && parsed.agent.enabled && parsed.agent.approved_process == selected.id() && parsed.agent.pipe_component == "safe", "Explicit paired selection was not parsed");
  phase = "held-identity-and-start";
  require(app.agent->selected_process_id() == selected.id() && app.agent->selected_creation_filetime() == selected.created(), "Selection is not actual held child"); app.start();
  phase = "denied-child";
  child_process denied{"denied-peer", app.name}; denied.resume(); app.until([&] { return denied.completed(); }, "Unapproved child timeout"); (void)denied.result();
  phase = "approved-child";
  selected.resume(); app.until([&] { return selected.completed(); }, "Approved child timeout"); (void)selected.result();
  phase = "exited-peer-admission";
  app.pump(); require(app.agent->closed() && !app.agent->start_bound_document([] { return true; }), "Exited held process regained admission");
  } catch (...) { phase_failure(phase, selected); throw; }
}
void fixed_summary(const char* fixture) {
  environment app{fixture}; child_process selected{"fixed-summary", app.name}; app.configure(selected.id());
  const auto page = app.bootstrap(); const auto task = app.page_start(page, "page-fixed"); app.start();
  selected.resume(); app.until([&] { return selected.completed(); }, "Local completion timeout"); const auto local = selected.result().at("payload");
  const auto terminal = app.page_terminal(page, task);
  require(terminal.at("state") == "succeeded" && page_summary_projection(terminal.at("result")) == local.at("result") && task != local.at("taskId"), "Two entrances differ in business result or task identity");
  deny(app.page(page, "task.query", {{"taskId", local.at("taskId")}}), "E_TASK_NOT_FOUND");
}
void cancel_case(const char* fixture) {
  environment app{fixture}; child_process selected{"cancel", app.name}; app.configure(selected.id()); app.start();
  selected.resume(); app.until([&] { return selected.completed(); }, "Cancel child timeout"); (void)selected.result();
}
void invalid_form(const char* fixture) {
  environment app{fixture}; child_process selected{"invalid-form", app.name}; app.configure(selected.id());
  const auto page = app.bootstrap();
  const auto good = app.page(page, "reference.form.submit", {{"name", "中文业务"}, {"notes", "有界草稿"}, {"category", "素材"}});
  require(good.at("ok") == true, "Page typed form failed"); app.start(); selected.resume();
  app.until([&] { return selected.completed(); }, "Form child timeout"); const auto local = selected.result().at("payload");
  require(local.at("accepted") == good.at("result").at("accepted") && local.at("message") == good.at("result").at("message") &&
      local.at("receiptId") != good.at("result").at("receiptId") && app.business->form_receipts.load() == 2, "Business receipts did not share actual context");
}
void revoked_document(const char* fixture) {
  environment app{fixture}; child_process selected{"revoked-document", app.name}; app.configure(selected.id()); const auto page = app.bootstrap(); app.start();
  selected.resume(); app.until([&] { return selected.signalled(); }, "Revocation child never entered"); app.native_window_current = false;
  // Real foreign-thread shutdown invokes only atomic admission close/stop/event
  // in the decorator (native control event, no HWND access); owner cleanup is
  // performed by the following real pump.
  std::thread revoke{[&] { app.agent->shutdown_session(page.at("sessionId").get<std::string>()); }}; revoke.join();
  require(app.agent->closed(), "Foreign revoke did not close admission"); app.stop(); selected.release();
  app.until([&] { return selected.completed(); }, "Revoked child timeout"); const auto task = selected.result().at("payload").at("taskId");
  deny(app.page(page, "reference.fixture.describe")); const auto next = app.bootstrap();
  deny(app.page(next, "task.query", {{"taskId", task}}), "E_TASK_NOT_FOUND");
  require(!app.agent->start_bound_document([] { return true; }), "New document automatically restored authorization");
}
void close_pending(const char* fixture) {
  environment app{fixture}; child_process selected{"close-pending", app.name}; app.configure(selected.id()); app.start(); selected.resume();
  app.until([&] { return selected.signalled(); }, "Partial-read child never entered"); app.native_window_current = false;
  std::thread close{[&] { app.agent->request_close(); }}; close.join(); app.stop(); app.stop();
  require(IsWindow(app.window), "Endpoint stop destroyed owner window prematurely"); selected.release();
  app.until([&] { return selected.completed(); }, "Partial-read child timeout"); (void)selected.result();
  // Environment destruction joins the actual Bridge before the root receipt.
}
void page_isolation(const char* fixture) {
  environment app{fixture};
  // The selected child stays suspended until factory, bootstrap and the page
  // task exist. Anonymous stdin transfers only its nonsecret correlation ID.
  child_process selected{"page-isolation", app.name, "stdin"}; app.configure(selected.id()); const auto page = app.bootstrap();
  deny(app.page(page, "reference.summary.local.start", {{"operationId", "page-forged"}, {"input", summary_input()}}));
  const auto task = app.page_start(page, "page-owned", 60000);
  selected.set_page_task(task); app.start(); selected.resume();
  app.until([&] { return selected.completed(); }, "Isolation child timeout"); (void)selected.result();
  app.bridge->shutdown_session(page.at("sessionId").get<std::string>());
}
}
int main(int argc, char** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  try {
    if (argc == 7 && std::string{argv[1]} == "--child") {
      const auto ready = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(argv[4])));
      const auto release = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(argv[5])));
      std::string task{argv[6]};
      if (task == "stdin") {
        task.clear();
        for (;;) {
          char byte{}; DWORD count{};
          require(ReadFile(GetStdHandle(STD_INPUT_HANDLE), &byte, 1, &count, nullptr) && count == 1, "Task correlation missing");
          if (byte == '\n') break;
          require(task.size() < 128, "Task correlation bound exceeded"); task.push_back(byte);
        }
        require(!task.empty() && task.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.:") == std::string::npos, "Task correlation malformed");
      }
      const auto payload = child_case(argv[2], argv[3], ready, release, task);
      std::cout << json{{"pid", std::to_string(GetCurrentProcessId())}, {"creationFileTime", std::to_string(creation(GetCurrentProcess()))}, {"payload", payload}}.dump() << '\n'; return 0;
    }
    require(argc == 3, "Exact case and fixture required"); const std::string name{argv[1]};
    if (name == "disabled-default-deny") disabled_default_deny(argv[2]);
    else if (name == "selected-peer") selected_peer(argv[2]); else if (name == "fixed-summary") fixed_summary(argv[2]);
    else if (name == "cancel") cancel_case(argv[2]); else if (name == "invalid-form") invalid_form(argv[2]);
    else if (name == "revoked-document") revoked_document(argv[2]); else if (name == "close-pending") close_pending(argv[2]);
    else if (name == "page-isolation") page_isolation(argv[2]); else throw std::runtime_error("Unknown case");
    std::cout << json{{"schema", "soui.reference-agent-native/1"}, {"case", name}, {"passed", true},
      {"pid", std::to_string(GetCurrentProcessId())}, {"creationFileTime", std::to_string(creation(GetCurrentProcess()))},
      {"children", child_receipts}, {"guiReadyVerified", false}, {"qualificationPassed", false}, {"releaseReady", false}}.dump() << '\n'; return 0;
  } catch (...) { std::cerr << "E_REFERENCE_AGENT_TEST\n"; return 1; }
}

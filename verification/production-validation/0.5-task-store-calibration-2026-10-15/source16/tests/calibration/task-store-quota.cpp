#include <windows.h>
#include <psapi.h>
#include <soui/json_bridge.h>
#include <soui/runtime/services/task/task_service.h>
#include <json.hpp>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using json = nlohmann::json;
namespace rt = soui::runtime;
namespace dm = soui::sdk::diagnostics;
using steady = std::chrono::steady_clock;
constexpr std::size_t history_count = 1024;
constexpr std::size_t message_bytes = 4096;
const std::string kind = "calibration" + std::string(117, 'k');
const std::string origin = "app://" + std::string(506, 'o');
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string decimal(std::uint64_t value) { return std::to_string(value); }
std::uint64_t process_created() {
  FILETIME created{}, exited{}, kernel{}, user{};
  require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE, "GetProcessTimes failed");
  return (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
}
struct workload {
  std::mutex mutex;
  std::condition_variable changed;
  bool open{false};
  std::atomic<unsigned> handled{0};
  soui::sdk::json_bridge bridge;
  json auth;
  std::uint64_t created{process_created()};
  steady::time_point deadline{steady::now() + std::chrono::minutes(5)};
  explicit workload(std::optional<rt::task_store_request_quota_policy> policy)
      : bridge(origin, dm::mode::scalar, policy) {
    require(bridge.register_task_handler(kind, [this](const rt::task_invocation& input, const rt::task_control& control) {
      const auto params = json::parse(input.parameters_json);
      if (params.at("hold").get<bool>()) {
        std::unique_lock<std::mutex> lock(mutex);
        require(changed.wait_until(lock, deadline, [this] { return open; }), "Blocked handler deadline");
      }
      require(control.report_progress(50, std::string(message_bytes, 'p')), "Maximum progress rejected");
      rt::task_outcome result;
      result.message.assign(message_bytes, 'm');
      json payload{{"payload", ""}};
      payload["payload"] = std::string(rt::bridge_transport::max_params_bytes - payload.dump().size(), 'r');
      result.result_json = payload.dump();
      require(result.result_json.size() == rt::bridge_transport::max_params_bytes, "Result size is not maximal");
      ++handled;
      return result;
    }), "Register calibration handler");
    const auto boot = json::parse(bridge.handle_json(R"({"requestId":"bootstrap","sessionId":"","origin":"","token":"","method":"bridge.bootstrap","params":{"protocol":"soui.bridge/1"}})"));
    require(boot.at("ok").get<bool>(), "Bootstrap failed"); auth = boot.at("result");
  }
  ~workload() { release(); } // Gate outlives the Bridge and its actual worker join.
  void release() { std::lock_guard<std::mutex> lock(mutex); open = true; changed.notify_all(); }
  json call(const char* method, json params) {
    require(steady::now() < deadline, "Calibration workload deadline");
    return json::parse(bridge.handle_json(json{{"requestId", "calibration"}, {"sessionId", auth.at("sessionId")},
      {"origin", auth.at("origin")}, {"token", auth.at("token")}, {"method", method},
      {"params", std::move(params)}, {"timeoutMs", 10000}}.dump()));
  }
  json start(std::size_t index, bool hold) {
    auto operation = "operation-" + std::to_string(index); operation.append(128 - operation.size(), 'x');
    json params{{"kind", kind}, {"operationId", operation}, {"input", {{"hold", hold}, {"payload", ""}}}};
    params["input"]["payload"] = std::string(rt::bridge_transport::max_params_bytes - params.dump().size(), 'i');
    require(params.dump().size() == rt::bridge_transport::max_params_bytes, "Request params are not maximal");
    return call("task.start", std::move(params));
  }
  std::string accepted(std::size_t index, bool hold) {
    auto response = start(index, hold);
    if (!response.value("ok", false)) throw std::runtime_error("Admission failed: " + response.dump());
    require(!response.at("result").at("duplicate").get<bool>(), "Unexpected duplicate admission");
    return response.at("result").at("taskId").get<std::string>();
  }
  void wait(const std::string& task) {
    for (;;) {
      const auto response = call("task.query", {{"taskId", task}});
      require(response.at("ok").get<bool>(), "Task query failed");
      const auto state = response.at("result").at("state").get<std::string>();
      if (state == "succeeded") return;
      require(state == "queued" || state == "running", "Task terminal failure");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  void sample(const char* phase) {
    rt::task_store_request_quota_snapshot quota;
    dm::bridge_runtime runtime;
    LARGE_INTEGER begin{}, end{}, frequency{};
    require(QueryPerformanceFrequency(&frequency) && QueryPerformanceCounter(&begin), "Sampling clock unavailable");
    const auto until = steady::now() + std::chrono::seconds(5);
    for (;;) {
      const auto q = bridge.read_task_store_request_quota(quota);
      const auto r = bridge.read_runtime_metrics(runtime);
      if (q == rt::task_store_request_quota_status::available && r == dm::status::ready) break;
      require((q == rt::task_store_request_quota_status::available || q == rt::task_store_request_quota_status::busy) &&
        (r == dm::status::ready || r == dm::status::busy), "Invalid native sample");
      require(steady::now() < until, "Native sample busy deadline"); std::this_thread::yield();
    }
    require(quota.owner_epoch_valid && quota.credit_valid && quota.denied_requests_exact, "Invalid quota counters");
    require(QueryPerformanceCounter(&end) != FALSE, "Sampling clock failed");
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory); DWORD handles{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) &&
      GetProcessHandleCount(GetCurrentProcess(), &handles), "Process metrics failed");
    json row{{"schema", "soui.native-task-store-calibration/1"}, {"phase", phase}, {"pid", GetCurrentProcessId()},
      {"creationFileTime", decimal(created)}, {"qpcBegin", decimal(begin.QuadPart)}, {"qpcEnd", decimal(end.QuadPart)},
      {"qpcFrequency", decimal(frequency.QuadPart)}, {"poolEpoch", decimal(quota.owner_epoch)},
      {"configuredLimit", quota.max_live_requested_bytes ? json(decimal(*quota.max_live_requested_bytes)) : json(nullptr)},
      {"chargedLive", decimal(quota.charged_live_requested_bytes)}, {"chargedPeak", decimal(quota.charged_peak_requested_bytes)},
      {"successfulLive", decimal(quota.successful_live_requested_bytes)}, {"successfulPeak", decimal(quota.successful_peak_requested_bytes)},
      {"quotaDenials", decimal(quota.denied_requests)}, {"storeEntries", runtime.task_service.store_entries},
      {"inflight", runtime.task_service.inflight}, {"queued", runtime.task_service.queued}, {"handled", handled.load()},
      {"privateBytes", decimal(memory.PrivateUsage)}, {"handles", handles}, {"productionDefaultQualified", false}};
    std::cout << row.dump() << '\n'; std::cout.flush();
    if (std::string(phase) == "full-inflight") require(runtime.task_service.inflight == rt::task_service::max_inflight, "Expected full inflight capacity");
    if (std::string(phase) != "empty") require(runtime.task_service.store_entries == history_count, "Expected full retained history");
  }
  void run() {
    sample("empty");
    for (std::size_t i = 0; i < history_count; ++i) wait(accepted(i, false));
    sample("full-history");
    std::vector<std::string> held;
    for (std::size_t i = 0; i < rt::task_service::max_inflight; ++i) held.push_back(accepted(history_count + i, true));
    sample("full-inflight");
    const auto denied = start(history_count + rt::task_service::max_inflight, true);
    require(!denied.at("ok").get<bool>() && denied.at("error").at("code") == "E_BUSY", "Inflight overflow did not reject");
    release(); for (const auto& task : held) wait(task);
    sample("released-inflight");
    for (std::size_t i = 0; i < 64; ++i) wait(accepted(history_count + rt::task_service::max_inflight + i, false));
    sample("retired-history");
  }
};
}
int main(int argc, char** argv) {
  try {
    require(argc == 1 || argc == 2, "Usage: SouiTaskStoreCalibration [LIMIT_BYTES]");
    std::optional<rt::task_store_request_quota_policy> policy;
    if (argc == 2) {
      const std::string input = argv[1]; std::uint64_t limit{};
      const auto parsed = std::from_chars(input.data(), input.data() + input.size(), limit);
      require(parsed.ec == std::errc{} && parsed.ptr == input.data() + input.size(), "Invalid exact unsigned quota");
      policy = rt::task_store_request_quota_policy{limit};
    }
    { workload instance(policy); instance.run(); }
    std::cout << "{\"schema\":\"soui.native-task-store-calibration/1\",\"phase\":\"destructor-returned\",\"postDestructionQuotaRead\":false,\"productionDefaultQualified\":false}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << json{{"schema", "soui.native-task-store-calibration/1"}, {"failed", true}, {"reason", error.what()}}.dump() << '\n';
    return 1;
  }
}

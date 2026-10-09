#include "demo_bridge_adapter.h"
#include <json.hpp>
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using json = nlohmann::json;
using adapter = soui::demo::demo_bridge_adapter;
using namespace std::chrono_literals;

void require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}

std::string code(const json& value) {
  return value.contains("error") ? value.at("error").value("code", "") : std::string{};
}

struct fixture {
  adapter app;
  unsigned sequence{0};

  json call(const json& auth, const char* method, json params, bool direct = false) {
    const auto id = "query-regression-" + std::to_string(++sequence);
    if (!direct) {
      return json::parse(app.handle_json(json{{"requestId", id},
          {"sessionId", auth.value("sessionId", "")}, {"origin", auth.value("origin", "")},
          {"token", auth.value("token", "")}, {"method", method}, {"params", std::move(params)}}.dump()));
    }
    soui::runtime::bridge_request request;
    request.request_id = id;
    request.session_id = auth.value("sessionId", "");
    request.origin = auth.value("origin", "");
    request.token = auth.value("token", "");
    request.method = method;
    request.params_json = params.dump();
    const auto response = app.handle(request);
    // Compare the actual public typed result without accessing the wire serializer.
    if (response.ok) return json{{"ok", true}, {"result", json::parse(response.payload_json)}};
    return json{{"ok", false}, {"error", {{"code", response.error_code},
                                         {"message", response.error_message}}}};
  }

  json bootstrap() {
    const auto response = call(json::object(), "bridge.bootstrap", {{"protocol", "soui.bridge/1"}});
    require(response.value("ok", false), "real Demo bootstrap failed: " + code(response));
    return response.at("result");
  }

  std::string start(const json& auth, const char* operation) {
    const auto response = call(auth, "task.start", {{"kind", "demo.count"},
        {"operationId", operation}, {"input", {{"steps", 2}}}});
    require(response.value("ok", false), "real SDK task.start failed: " + code(response));
    return response.at("result").at("taskId").get<std::string>();
  }

  json query(const json& auth, const std::string& task, bool direct = false) {
    const auto response = call(auth, "task.query", {{"taskId", task}}, direct);
    require(response.value("ok", false), std::string{"owned taskId "} +
        (direct ? "direct" : "JSON") + " query rejected: " + code(response));
    require(response.at("result").value("taskId", "") == task, "query returned a different real task");
    return response.at("result");
  }

  json terminal(const json& auth, const std::string& task, bool direct = false) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    for (;;) {
      const auto value = query(auth, task, direct);
      const auto state = value.value("state", "");
      if (state == "succeeded" || state == "failed" || state == "cancelled") return value;
      require(std::chrono::steady_clock::now() < deadline, "real SDK worker did not finish within five seconds");
      std::this_thread::sleep_for(10ms);
    }
  }
};

void owner_entrypoint(bool direct) {
  fixture f;
  const auto auth = f.bootstrap();
  const auto task = f.start(auth, direct ? "direct-owned-query" : "json-owned-query");
  require(task.compare(0, 9, "sdk-task-") == 0, "start did not use actual SDK task storage");
  const auto completed = f.terminal(auth, task, direct);
  require(completed.value("state", "") == "succeeded" && completed.at("result").at("sum") == 3,
          "query lost the result of the real asynchronous SDK worker");
}

void strict_selectors() {
  fixture f;
  const auto auth = f.bootstrap();
  const auto task = f.start(auth, "strict-owned-query");
  const std::vector<json> invalid{
      json::object(), {{"taskId", nullptr}}, {{"taskId", true}}, {{"taskId", 1}},
      {{"taskId", task}, {"extra", 1}}, {{"taskId", task}, {"sessionId", auth.at("sessionId")}},
      {{"taskId", task}, {"origin", auth.at("origin")}}, {{"taskId", task}, {"token", "forged"}},
      {{"taskId", task}, {"userGesture", true}}, {{"taskId", task}, {"operationId", "strict-owned-query"}},
      // Alternate operation lookup remains outside this legacy generated Demo contract.
      {{"operationId", "strict-owned-query"}}, {{"taskId", std::string(129, 'a').insert(0, "sdk-task-")}},
      {{"taskId", "sdk-task-/invalid"}}};
  for (const bool direct : {false, true}) {
    for (const auto& params : invalid) {
      const auto response = f.call(auth, "task.query", params, direct);
      require(!response.value("ok", true) && code(response) == "E_INVALID_PARAMS",
              "query accepted a malformed selector or credential parameter");
    }
    require(f.query(auth, task, direct).value("taskId", "") == task,
            "invalid queries changed owner task visibility");
  }
}

void scope_ownership() {
  fixture f;
  const auto owner = f.bootstrap();
  const auto task = f.start(owner, "scope-owned-query");
  auto stranger = owner;
  stranger["sessionId"] = "query-stranger";
  stranger["token"] = f.app.issue_session("query-stranger", owner.at("origin"), {"task.query"});
  auto same_session = owner;
  same_session["token"] = f.app.issue_session(owner.at("sessionId"), owner.at("origin"), {"task.query"});
  auto no_capability = owner;
  no_capability["sessionId"] = "query-no-capability";
  no_capability["token"] = f.app.issue_session("query-no-capability", owner.at("origin"), {"bridge.probe"});
  auto forged = owner;
  forged["token"] = "not-a-real-native-bearer";
  for (const bool direct : {false, true}) {
    for (const auto& auth : {stranger, same_session}) {
      const auto response = f.call(auth, "task.query", {{"taskId", task}}, direct);
      require(!response.value("ok", true) && code(response) == "E_TASK_NOT_FOUND",
              "different live native grant stole an SDK-owned task");
    }
    for (const auto& auth : {no_capability, forged}) {
      const auto response = f.call(auth, "task.query", {{"taskId", task}}, direct);
      require(!response.value("ok", true) && code(response) == "E_UNAUTHORIZED",
              "query bypassed native bearer/capability resolution");
    }
    (void)f.query(owner, task, direct);
  }
  f.app.shutdown_session(owner.at("sessionId"));
  for (const bool direct : {false, true}) {
    const auto response = f.call(owner, "task.query", {{"taskId", task}}, direct);
    require(!response.value("ok", true) && code(response) == "E_UNAUTHORIZED",
            "revoked native session retained task query authority");
  }
}

struct owned_files {
  const std::filesystem::path directory = std::filesystem::current_path() /
      ("demo-task-query-files-" + std::to_string(GetCurrentProcessId()));
  const std::filesystem::path file = directory / "a.txt";
  bool owned{false};
  owned_files() {
    require(std::filesystem::create_directory(directory), "fresh regression file directory already exists");
    owned = true;
    try {
      std::ofstream output(file, std::ios::binary);
      output << "actual scanner input\n";
      output.flush();
      require(output.good(), "could not write real scanner input");
      output.close();
      require(!output.fail(), "could not close real scanner input");
    } catch (...) {
      std::error_code file_error, directory_error;
      (void)std::filesystem::remove(file, file_error);
      const bool removed = std::filesystem::remove(directory, directory_error);
      owned = false;
      if (file_error || directory_error || !removed)
        throw std::runtime_error("failed scanner input construction could not clean acquired files");
      throw;
    }
  }
  ~owned_files() {
    if (!owned) return;
    std::error_code error;
    std::filesystem::remove(file, error);
    std::filesystem::remove(directory, error); // Nonrecursive; never removes unexpected contents.
  }
  void clear() {
    require(owned && file.parent_path() == directory && directory.parent_path() == std::filesystem::current_path(),
            "owned scanner fixture path changed");
    require(std::filesystem::remove(file) && std::filesystem::remove(directory),
            "real scanner input cleanup failed");
    owned = false;
  }
};

void legacy_scan_task() {
  owned_files files;
  fixture f; // Its actual services join their workers before the files are removed.
  const auto owner = f.bootstrap();
  const auto root = f.app.register_root({owner.at("sessionId"), owner.at("origin"), owner.at("token")},
                                        files.directory);
  require(!root.empty(), "real opaque scanner root registration rejected native authorization");
  const auto started = f.call(owner, "demo.scan.start", {{"rootToken", root},
      {"filter", ""}, {"operationId", "legacy-scan-owned-query"}});
  require(started.value("ok", false), "real legacy scanner start failed: " + code(started));
  const auto task = started.at("result").at("taskId").get<std::string>();
  require(task.compare(0, 9, "sdk-task-") != 0, "legacy scanner unexpectedly used public SDK task storage");
  for (const bool direct : {false, true}) {
    const auto result = f.terminal(owner, task, direct);
    require(result.value("state", "") == "succeeded" && result.at("entries").is_array(),
            "task.query lost the real legacy scanner result");
    auto stranger = owner;
    stranger["sessionId"] = direct ? "legacy-direct-stranger" : "legacy-json-stranger";
    stranger["token"] = f.app.issue_session(stranger.at("sessionId"), owner.at("origin"), {"task.query"});
    const auto rejected = f.call(stranger, "task.query", {{"taskId", task}}, direct);
    require(!rejected.value("ok", true) && code(rejected) == "E_TASK_NOT_FOUND",
            "legacy query ignored the authenticated envelope owner");
  }
  f.app.shutdown_session(owner.at("sessionId"));
  files.clear();
}
} // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 2, "exactly one regression case is required");
    const std::string name = argv[1];
    if (name == "json-task-id") owner_entrypoint(false);
    else if (name == "direct-task-id") owner_entrypoint(true);
    else if (name == "strict-selectors") strict_selectors();
    else if (name == "scope-ownership") scope_ownership();
    else if (name == "legacy-scan-task") legacy_scan_task();
    else throw std::runtime_error("unknown regression case");
    std::cout << json{{"case", name}, {"passed", true}, {"actualSdkAndDemo", true},
                     {"operationIdDemoContractSupported", false}, {"qualificationPassed", false}}.dump() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "DEMO-TASK-QUERY-FAILURE " << error.what() << '\n';
    return 1;
  }
}

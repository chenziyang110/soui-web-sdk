#include "demo_bridge_adapter.h"
#include "native_media_preview.h"
#include "native_media_encoder.h"
#include <soui/runtime/services/fileinsight-service/fileinsight-service.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wincodec.h>
#include <objidl.h>
#include <propvarutil.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <random>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "../generated/contracts/soui_native.generated.h"
#include "../../../third_party/include/json/json.hpp"
#include "../../../third_party/sources/zlib/zlib.h"

namespace soui::demo {
namespace {

using json = nlohmann::json;

bool matches_kind(const json& value, std::string_view kind) {
  if (kind == "string") return value.is_string();
  if (kind == "boolean") return value.is_boolean();
  if (kind == "number") return value.is_number_integer();
  if (kind == "array") return value.is_array();
  if (kind == "object") return value.is_object();
  return false;
}

bool validate_contract_params(const std::string& method, const json& params, std::string& error) {
#ifdef SOUI_BRIDGE_BENCHMARK
  if (method == "bridge.benchmark.ping") {
    if (params.is_object() && params.empty()) return true;
    error = "Diagnostic ping requires empty params.";
    return false;
  }
#endif
  const bool known_method = std::any_of(std::begin(soui::generated::kMethods),
                                        std::end(soui::generated::kMethods),
                                        [&](const auto& item) { return method == item.name; });
  if (!known_method) return true;
  if (!params.is_object()) {
    error = "params must be an object.";
    return false;
  }
  std::size_t expected_count = 0;
  for (const auto& field : soui::generated::kParams) {
    if (method != field.method) continue;
    ++expected_count;
    const auto found = params.find(field.name);
    if (found == params.end() || !matches_kind(*found, field.type)) {
      error = std::string{"params."} + field.name + " has the wrong type or is missing.";
      return false;
    }
  }
  if (params.size() != expected_count) {
    error = "params contains missing or unknown fields.";
    return false;
  }
  return true;
}

std::string invalid_params_response(const json& envelope, const std::string& message) {
  return json{{"requestId", envelope.value("requestId", "")}, {"ok", false},
              {"error", {{"code", "E_INVALID_PARAMS"}, {"message", message}}}}.dump();
}

std::string bridge_error_json(std::string request_id, const char* code, const char* message) {
  return json{{"requestId", std::move(request_id)}, {"ok", false},
              {"error", {{"code", code}, {"message", message}}}}.dump();
}

std::string json_escape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8U);
  for (const char character : value) {
    if (character == '\\' || character == '"') escaped.push_back('\\');
    if (character == '\n') { escaped += "\\n"; continue; }
    escaped.push_back(character);
  }
  return escaped;
}

std::string payload_error(const std::string& code, const std::string& message) {
  return "{\"error\":\"" + json_escape(code) + "\",\"message\":\"" + json_escape(message) + "\"}";
}

std::string make_bootstrap_session_id() {
  std::random_device random;
  std::ostringstream stream;
  stream << "session-" << std::hex << random() << random();
  return stream.str();
}

#ifdef _WIN32
std::string wide_to_utf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (length <= 0) return {};
  std::string result(static_cast<std::size_t>(length), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), length, nullptr, nullptr) <= 0) {
    return {};
  }
  return result;
}
#endif

std::string state_name(soui::runtime::task_state state) {
  switch (state) {
    case soui::runtime::task_state::queued: return "queued";
    case soui::runtime::task_state::running: return "running";
    case soui::runtime::task_state::timeout_observed: return "timeout";
    case soui::runtime::task_state::cancel_requested: return "cancel-pending";
    case soui::runtime::task_state::succeeded: return "succeeded";
    case soui::runtime::task_state::failed: return "failed";
    case soui::runtime::task_state::cancelled: return "cancelled";
    case soui::runtime::task_state::partial: return "partial";
  }
  return "unknown";
}

bool terminal_task_state(soui::runtime::task_state state) {
  return state == soui::runtime::task_state::succeeded ||
         state == soui::runtime::task_state::failed ||
         state == soui::runtime::task_state::cancelled ||
         state == soui::runtime::task_state::partial;
}

std::uint32_t crc32_bytes(const std::uint8_t* data, std::size_t size) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& input, std::size_t offset) {
  return static_cast<std::uint16_t>(input[offset] | (static_cast<std::uint16_t>(input[offset + 1]) << 8U));
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& input, std::size_t offset) {
  return static_cast<std::uint32_t>(input[offset]) |
         (static_cast<std::uint32_t>(input[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(input[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(input[offset + 3]) << 24U);
}

json scan_page_json(const soui::runtime::scan_response& page) {
  json entries = json::array();
  for (const auto& entry : page.entries) {
    entries.push_back({{"path", entry.relative_path},
                       {"directory", entry.directory},
                       {"size", entry.size}});
  }
  json groups = json::array();
  for (const auto& group : page.groups) {
    groups.push_back({{"extension", group.extension},
                      {"fileCount", group.file_count},
                      {"totalBytes", group.total_bytes}});
  }
  return {{"taskId", page.task_id},
          {"entries", std::move(entries)},
          {"cursor", page.next_cursor},
          {"hasMore", page.has_more},
          {"message", page.message},
          {"skipped", page.skipped_count},
          {"errors", page.error_count},
          {"issues", page.issues},
          {"fileCount", page.file_count},
          {"totalBytes", page.total_bytes},
          {"groups", std::move(groups)}};
}

std::vector<std::string> expand_capabilities(const std::vector<std::string>& capabilities) {
  std::vector<std::string> expanded;
  expanded.reserve(capabilities.size() + 4U);
  const auto add_unique = [&expanded](const std::string& capability) {
    if (std::find(expanded.begin(), expanded.end(), capability) == expanded.end()) expanded.push_back(capability);
  };
  const bool legacy_filesystem_read =
      std::find(capabilities.begin(), capabilities.end(), "filesystem.read") != capabilities.end();
  for (const auto& capability : capabilities) add_unique(capability);
  if (legacy_filesystem_read) {
    add_unique("desktop.dialog.pickDirectory");
    add_unique("demo.scan");
    add_unique("filesystem.scan");
    add_unique("task.query");
    add_unique("task.cancel");
    add_unique("task.subscribe");
  }
  return expanded;
}

// The packaged sample is registered through the same opaque-root path as a
// user-selected directory.  It gives the demo a deterministic native action
// without bypassing permission checks or exposing a filesystem path to Web.
std::filesystem::path bundled_fileinsight_fixture(const wchar_t* fixture_name) {
#ifdef _WIN32
  wchar_t module_path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  std::error_code error;
  const auto fixture = std::filesystem::path(std::wstring(module_path, length)).parent_path() /
                       "resources" / "fixtures" / fixture_name;
  if (std::filesystem::is_directory(fixture, error) && !error) return fixture;
#endif
  return {};
}

}  // namespace

demo_bridge_adapter::demo_bridge_adapter()
    : scanner_(permissions_, tasks_), picker_(permissions_), windows_(permissions_), clipboard_(permissions_),
      transport_(registry_, permissions_) {
  (void)public_tasks_.register_handler("demo.count", [](const soui::runtime::task_invocation& invocation,
                                                         const soui::runtime::task_control& control) {
    const auto input = json::parse(invocation.parameters_json, nullptr, false);
    if (!input.is_object() || input.size() != 1U || !input.contains("steps") ||
        !input["steps"].is_number_integer()) {
      return soui::runtime::task_outcome{soui::runtime::result_kind::failure, "{}", "Invalid count input."};
    }
    const auto steps = input["steps"].get<int>();
    if (steps < 1 || steps > 200) {
      return soui::runtime::task_outcome{soui::runtime::result_kind::failure, "{}", "steps must be 1..200."};
    }
    int sum = 0;
    for (int step = 1; step <= steps; ++step) {
      if (control.cancellation_requested())
        return soui::runtime::task_outcome{soui::runtime::result_kind::task_cancelled, "{}", "Count cancelled."};
      sum += step;
      if (step == steps || step % 5 == 0)
        (void)control.report_progress(static_cast<std::uint32_t>(step * 100 / steps), "Counting in native SDK worker.");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return soui::runtime::task_outcome{soui::runtime::result_kind::success,
                                       json{{"count", steps}, {"sum", sum}}.dump(), "Count complete."};
  });
  register_methods();
  soui::modules::fileinsight::FileinsightService::register_methods(registry_, scanner_);
}

demo_bridge_adapter::~demo_bridge_adapter() {
  shutdown_media_jobs({});
  shutdown_workload_jobs({});
  shutdown_media_preview_jobs({});
}

void demo_bridge_adapter::set_owner_window(void* owner_window) noexcept {
  owner_window_ = owner_window;
}

void demo_bridge_adapter::set_event_sink(event_sink sink) {
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    event_sink_ = std::move(sink);
  }
  public_tasks_.set_event_sink([this](std::string_view event_text) {
    event_sink sink_copy;
    {
      std::lock_guard<std::mutex> lock(subscription_mutex_);
      sink_copy = event_sink_;
    }
    if (sink_copy) sink_copy(event_text);
  });
  scanner_.set_event_sink([this](std::string_view event_text) {
    const auto event = json::parse(event_text.begin(), event_text.end(), nullptr, false);
    if (!event.is_object()) return;
    const auto session_id = event.value("sessionId", "");
    const auto task_id = event.value("taskId", "");
    const auto event_name = event.value("eventName", "");
    event_sink sink_copy;
    {
      std::lock_guard<std::mutex> lock(subscription_mutex_);
      const bool subscribed = std::any_of(subscriptions_.begin(), subscriptions_.end(),
                                          [&](const auto& entry) {
        const auto& binding = entry.second;
        return binding.context.session_id == session_id && binding.task_id == task_id &&
               binding.event_name == event_name;
      });
      if (!subscribed) return;
#ifdef SOUI_PROGRESS_DEDUP
      const auto key = session_id + ":" + task_id;
      const auto payload = event.find("payload");
      if (event_name == "task.progress" && payload != event.end() && payload->is_object() && payload->value("state", "") == "running") {
        const auto content = payload->dump();
        const auto previous = running_payloads_.find(key);
        if (previous != running_payloads_.end() && previous->second == content) return;
        if (running_payloads_.size() >= 256U && previous == running_payloads_.end()) running_payloads_.erase(running_payloads_.begin());
        running_payloads_[key] = content;
      } else {
        running_payloads_.erase(key);
      }
#endif
      sink_copy = event_sink_;
    }
    if (sink_copy) sink_copy(event_text);
  });
}

void demo_bridge_adapter::shutdown_session(const std::string& session_id) {
  requested_frame_rate_.store(60);
  public_tasks_.shutdown_session(session_id);
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    if (active_bootstrap_session_ == session_id) active_bootstrap_session_.clear();
    request_rates_.erase(session_id);
#ifdef SOUI_PROGRESS_DEDUP
    const auto prefix = session_id + ":";
    for (auto iterator = running_payloads_.begin(); iterator != running_payloads_.end();) {
      if (iterator->first.compare(0, prefix.size(), prefix) == 0) iterator = running_payloads_.erase(iterator);
      else ++iterator;
    }
#endif
    for (auto iterator = subscriptions_.begin(); iterator != subscriptions_.end();) {
      if (iterator->second.context.session_id == session_id) iterator = subscriptions_.erase(iterator);
      else ++iterator;
    }
  }
  scanner_.shutdown_session(session_id);
  shutdown_media_jobs(session_id);
  shutdown_workload_jobs(session_id);
  shutdown_media_preview_jobs(session_id);
  shutdown_media_files(session_id);
  permissions_.revoke_session(session_id);
}

std::string demo_bridge_adapter::handle_json(std::string_view json_text, bool native_user_gesture) {
  if (json_text.empty() || json_text.size() > soui::runtime::bridge_transport::max_message_bytes) {
    return bridge_error_json({}, "E_LIMIT_MESSAGE_SIZE", "The bridge message exceeds the allowed size.");
  }
  try {
    auto envelope = json::parse(json_text.begin(), json_text.end());
    if (envelope.is_object() && envelope.value("method", "") == "bridge.bootstrap") {
      static const std::unordered_set<std::string> allowed{
          "requestId", "sessionId", "origin", "token", "method", "params"};
      for (const auto& field : envelope.items()) {
        if (allowed.find(field.key()) == allowed.end()) {
          return bridge_error_json(envelope.value("requestId", ""), "E_SCHEMA_INVALID",
                                   "bridge.bootstrap contains an unknown field.");
        }
      }
      const auto params = envelope.find("params");
      const bool valid_protocol = params != envelope.end() && params->is_object() &&
                                  params->size() == 1U &&
                                  params->value("protocol", "") == "soui.bridge/1";
      const bool anonymous = envelope.value("sessionId", "").empty() &&
                             envelope.value("origin", "").empty() &&
                             envelope.value("token", "").empty();
      const bool valid_request_id = envelope.contains("requestId") && envelope["requestId"].is_string() &&
                                    !envelope["requestId"].get_ref<const std::string&>().empty();
      json response;
      response["requestId"] = envelope.value("requestId", "");
      if (!valid_protocol || !anonymous || !valid_request_id) {
        response["ok"] = false;
        response["error"] = {{"code", "E_UNAUTHORIZED"},
                             {"message", "bridge.bootstrap requires trusted local protocol bootstrap."}};
        return response.dump();
      }
      const std::string session_id = make_bootstrap_session_id();
      {
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        if (!active_bootstrap_session_.empty()) {
          return bridge_error_json(envelope.value("requestId", ""), "E_UNAUTHORIZED",
                                   "The trusted WebView already owns an active bridge session.");
        }
        active_bootstrap_session_ = session_id;
      }
      const std::string origin = "app://demo";
      const auto token = issue_session(session_id, origin, {"filesystem.read", "task.start", "bridge.probe", "desktop.rendering.control",
          "media.convert",
          "desktop.media.pickFile",
          "desktop.workload",
          "desktop.window.setTitle", "desktop.window.beginDrag", "desktop.window.minimize",
          "desktop.window.toggleMaximize", "desktop.window.close", "desktop.clipboard.writeText"});
      std::string sample_root_token;
      std::string alternate_sample_root_token;
      if (const auto fixture = bundled_fileinsight_fixture(L"fileinsight-fixture"); !fixture.empty()) {
        sample_root_token = register_root({session_id, origin, token}, fixture);
      }
      if (const auto fixture = bundled_fileinsight_fixture(L"fileinsight-fixture-alt"); !fixture.empty()) {
        alternate_sample_root_token = register_root({session_id, origin, token}, fixture);
      }
      requested_frame_rate_.store(preferred_frame_rate_.load());
      response["ok"] = true;
      bool react_mode = false;
#ifdef _WIN32
      char react_option[2] = {};
      react_mode = GetEnvironmentVariableA("SOUI_REACT_MODE", react_option, sizeof(react_option)) == 1 &&
                   react_option[0] == '1';
#endif
      response["result"] = {{"sessionId", session_id}, {"origin", origin}, {"token", token},
                             {"reactMode", react_mode}, {"sampleRootToken", sample_root_token},
                             {"alternateSampleRootToken", alternate_sample_root_token}};
      return response.dump();
    }
    if (envelope.is_object() && envelope["params"].is_object()) {
      auto& params = envelope["params"];
      const auto method = envelope.value("method", "");
      std::string validation_error;
      if (!validate_contract_params(method, params, validation_error)) {
        return invalid_params_response(envelope, validation_error);
      }
      const auto session_id = envelope.value("sessionId", "");
      const auto origin = envelope.value("origin", "");
      const auto token = envelope.value("token", "");
      const auto capability = registry_.capability_for(method);
      const bool authorized_session = capability != nullptr &&
          permissions_.authorize(session_id, origin, token, *capability);
      if (authorized_session && !allow_request(session_id)) {
        return bridge_error_json(envelope.value("requestId", ""), "E_LIMIT_RATE",
                                 "The bridge request rate limit was exceeded.");
      }
      if (method == "desktop.dialog.pickDirectory" || method == "desktop.media.pickFile" || method == "desktop.media.saveOutput" ||
          method == "desktop.clipboard.writeText" ||
          method == "desktop.window.beginDrag" || method == "desktop.window.minimize" ||
          method == "desktop.window.toggleMaximize" || method == "desktop.window.close") {
        params["userGesture"] = native_user_gesture;
      }
      if (method != "task.start") {
        params["sessionId"] = session_id;
        params["origin"] = origin;
        params["token"] = token;
      }
      return transport_.handle_json(envelope.dump());
    }
  } catch (const json::parse_error&) {
    return transport_.handle_json(json_text);
  } catch (const json::exception&) {
    return bridge_error_json({}, "E_SCHEMA_INVALID", "The bridge envelope contains an invalid field type.");
  }
  return transport_.handle_json(json_text);
}

std::string demo_bridge_adapter::issue_session(const std::string& session_id, const std::string& origin,
                                               const std::vector<std::string>& capabilities) {
  return permissions_.issue(session_id, origin, expand_capabilities(capabilities));
}

std::string demo_bridge_adapter::register_root(const soui::runtime::scan_request_context& context,
                                               const std::filesystem::path& root) {
  return scanner_.register_root(context, root);
}

soui::runtime::bridge_response demo_bridge_adapter::handle(const soui::runtime::bridge_request& request) const {
  const auto parsed_params = json::parse(request.params_json.empty() ? "{}" : request.params_json,
                                         nullptr, false);
  std::string validation_error;
  if (!validate_contract_params(request.method, parsed_params, validation_error)) {
    soui::runtime::bridge_response rejected;
    rejected.request_id = request.request_id;
    rejected.session_id = request.session_id;
    rejected.correlation_id = request.request_id;
    rejected.operation_id = request.operation_id;
    rejected.error_code = "E_INVALID_PARAMS";
    rejected.error_message = validation_error;
    return rejected;
  }
  auto bound_request = request;
  bound_request.params_json = augment_context(request);
  auto response = transport_.handle(bound_request);
  if (response.ok && response.payload_json.find("\"error\":") != std::string::npos) {
    const std::regex code_pattern("\\\"error\\\":\\\"([^\\\"]+)\\\"");
    std::smatch match;
    if (std::regex_search(response.payload_json, match, code_pattern) && match.size() > 1U) {
      response.ok = false;
      response.error_code = match[1].str();
      response.payload_json.clear();
    }
  }
  return response;
}

soui::runtime::scan_response demo_bridge_adapter::read_scan_page(
    const soui::runtime::scan_request_context& context, const std::string& task_id, std::string cursor,
    std::size_t page_size) {
  return scanner_.read_page(context, task_id, std::move(cursor), page_size);
}

bool demo_bridge_adapter::owns_media_task(const soui::runtime::scan_request_context& context,
                                          const std::string& task_id) const {
  std::lock_guard<std::mutex> lock(media_mutex_);
  const auto found = media_jobs_.find(task_id);
  return found != media_jobs_.end() && found->second->context.session_id == context.session_id &&
         found->second->context.origin == context.origin && found->second->context.token == context.token;
}

std::string demo_bridge_adapter::media_task_json(const soui::runtime::scan_request_context& context,
                                                 const std::string& task_id) const {
  std::filesystem::path output;
  {
    std::lock_guard<std::mutex> lock(media_mutex_);
    const auto found = media_jobs_.find(task_id);
    if (found == media_jobs_.end() || found->second->context.session_id != context.session_id ||
        found->second->context.origin != context.origin || found->second->context.token != context.token) {
      return payload_error("E_TASK_NOT_FOUND", "The media task is not valid for this session.");
    }
    output = found->second->output;
  }
  const auto task = tasks_.query(task_id);
  if (!task.has_value()) return payload_error("E_TASK_NOT_FOUND", "The media task does not exist.");
  return json{{"taskId", task->task_id},
              {"state", state_name(task->state)},
              {"progress", task->progress},
              {"output", output.filename().generic_string()},
              {"message", task->message}}.dump();
}

bool demo_bridge_adapter::owns_workload_task(const soui::runtime::scan_request_context& context,
                                             const std::string& task_id) const {
  std::lock_guard<std::mutex> lock(workload_mutex_);
  const auto found = workload_jobs_.find(task_id);
  return found != workload_jobs_.end() && found->second->context.session_id == context.session_id &&
         found->second->context.origin == context.origin && found->second->context.token == context.token;
}

std::string demo_bridge_adapter::workload_task_json(const soui::runtime::scan_request_context& context,
                                                    const std::string& task_id) const {
  std::string kind;
  std::string metrics;
  {
    std::lock_guard<std::mutex> lock(workload_mutex_);
    const auto found = workload_jobs_.find(task_id);
    if (found == workload_jobs_.end() || found->second->context.session_id != context.session_id ||
        found->second->context.origin != context.origin || found->second->context.token != context.token) {
      return payload_error("E_TASK_NOT_FOUND", "The workload task is not valid for this session.");
    }
    kind = found->second->kind;
    metrics = found->second->metrics_json.empty() ? "{}" : found->second->metrics_json;
  }
  const auto task = tasks_.query(task_id);
  if (!task.has_value()) return payload_error("E_TASK_NOT_FOUND", "The workload task does not exist.");
  return json{{"taskId", task->task_id},
              {"state", state_name(task->state)},
              {"progress", task->progress},
              {"kind", kind},
              {"metrics", metrics},
              {"message", task->message}}.dump();
}

bool demo_bridge_adapter::owns_media_preview_task(
    const soui::runtime::scan_request_context& context, const std::string& task_id) const {
  std::lock_guard<std::mutex> lock(media_preview_mutex_);
  const auto found = media_preview_jobs_.find(task_id);
  return found != media_preview_jobs_.end() && found->second->context.session_id == context.session_id &&
         found->second->context.origin == context.origin && found->second->context.token == context.token;
}

std::string demo_bridge_adapter::media_preview_task_json(
    const soui::runtime::scan_request_context& context, const std::string& task_id) const {
  media_preview_result preview;
  {
    std::lock_guard<std::mutex> lock(media_preview_mutex_);
    const auto found = media_preview_jobs_.find(task_id);
    if (found == media_preview_jobs_.end() || found->second->context.session_id != context.session_id ||
        found->second->context.origin != context.origin || found->second->context.token != context.token) {
      return payload_error("E_TASK_NOT_FOUND", "The media preview task is not valid for this session.");
    }
    preview = found->second->result;
  }
  const auto task = tasks_.query(task_id);
  if (!task.has_value()) return payload_error("E_TASK_NOT_FOUND", "The media preview task does not exist.");
  return json{{"taskId", task->task_id}, {"state", state_name(task->state)},
              {"progress", task->progress}, {"supported", preview.supported},
              {"frameData", preview.frame_data}, {"width", preview.width},
              {"height", preview.height}, {"durationMs", preview.duration_ms},
              {"decoder", preview.decoder}, {"message", task->message.empty() ? preview.message : task->message}}
      .dump();
}

void demo_bridge_adapter::run_media_preview_job(media_preview_job* job) {
  if (job == nullptr || !tasks_.mark_running(job->task_id)) return;
  emit_media_preview_event(*job, "running", 0, "Native media preview started.");
  if (job->cancel_requested.load(std::memory_order_relaxed)) {
    (void)tasks_.confirm_cancelled(job->task_id, "Native media preview cancelled.");
    emit_media_preview_event(*job, "cancelled", 0, "Native media preview cancelled.");
    return;
  }
  auto preview = decode_media_preview(job->source);
  {
    std::lock_guard<std::mutex> lock(media_preview_mutex_);
    const auto found = media_preview_jobs_.find(job->task_id);
    if (found != media_preview_jobs_.end()) found->second->result = std::move(preview);
  }
  if (job->cancel_requested.load(std::memory_order_relaxed)) {
    (void)tasks_.confirm_cancelled(job->task_id, "Native media preview cancelled.");
    emit_media_preview_event(*job, "cancelled", 0, "Native media preview cancelled.");
    return;
  }
  media_preview_result result;
  {
    std::lock_guard<std::mutex> lock(media_preview_mutex_);
    const auto found = media_preview_jobs_.find(job->task_id);
    if (found != media_preview_jobs_.end()) result = found->second->result;
  }
  (void)tasks_.update_progress(job->task_id, 100);
  const auto kind = result.supported ? soui::runtime::result_kind::success
                                     : soui::runtime::result_kind::partial;
  (void)tasks_.complete(job->task_id, kind, result.message);
  emit_media_preview_event(*job, result.supported ? "succeeded" : "partial", 100, result.message);
}

void demo_bridge_adapter::emit_media_preview_event(const media_preview_job& job, const std::string& state,
                                                   unsigned progress, const std::string& message) const {
  event_sink sink_copy;
  std::uint64_t sequence = 0;
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    const bool subscribed = std::any_of(subscriptions_.begin(), subscriptions_.end(), [&](const auto& entry) {
      const auto& binding = entry.second;
      return binding.context.session_id == job.context.session_id && binding.task_id == job.task_id &&
             binding.event_name == "task.progress";
    });
    if (!subscribed) return;
    sink_copy = event_sink_;
    sequence = next_media_event_sequence_++;
  }
  if (!sink_copy) return;
  sink_copy(json{{"sessionId", job.context.session_id}, {"sequence", sequence},
                 {"eventName", "task.progress"}, {"taskId", job.task_id},
                 {"payload", {{"state", state}, {"progress", progress}, {"message", message},
                               {"kind", "media-preview"},
                               {"indeterminate", state == "accepted" || state == "running" ||
                                                    state == "cancel-pending"}}}}
                 .dump());
}

void demo_bridge_adapter::join_finished_media_preview_jobs() {
  std::vector<std::unique_ptr<media_preview_job>> finished;
  {
    std::lock_guard<std::mutex> lock(media_preview_mutex_);
    for (auto iterator = media_preview_jobs_.begin(); iterator != media_preview_jobs_.end();) {
      const auto task = tasks_.query(iterator->first);
      if (task.has_value() && terminal_task_state(task->state)) {
        finished.push_back(std::move(iterator->second));
        iterator = media_preview_jobs_.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  for (auto& job : finished) if (job && job->worker.joinable()) job->worker.join();
}

void demo_bridge_adapter::shutdown_media_preview_jobs(const std::string& session_id) {
  std::vector<std::unique_ptr<media_preview_job>> jobs;
  {
    std::lock_guard<std::mutex> lock(media_preview_mutex_);
    for (auto iterator = media_preview_jobs_.begin(); iterator != media_preview_jobs_.end();) {
      if (session_id.empty() || iterator->second->context.session_id == session_id) {
        iterator->second->cancel_requested.store(true, std::memory_order_relaxed);
        jobs.push_back(std::move(iterator->second));
        iterator = media_preview_jobs_.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  for (auto& job : jobs) if (job && job->worker.joinable()) job->worker.join();
}

std::filesystem::path demo_bridge_adapter::bundled_sample_mp4() {
#ifdef _WIN32
  wchar_t module_path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  std::error_code error;
  const auto assets = std::filesystem::path(std::wstring(module_path, length)).parent_path() /
                      "resources" / "assets";
  if (!std::filesystem::is_directory(assets, error) || error) return {};
  for (std::filesystem::recursive_directory_iterator iterator(assets, error), end;
       iterator != end && !error; iterator.increment(error)) {
    if (!iterator->is_regular_file(error) || error) continue;
    const auto extension = iterator->path().extension().wstring();
    if (extension == L".mp4" || extension == L".MP4") return iterator->path();
  }
#endif
  return {};
}

std::filesystem::path demo_bridge_adapter::bundled_4k_sample_mp4() {
#ifdef _WIN32
  wchar_t module_path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  const auto assets = std::filesystem::path(std::wstring(module_path, length)).parent_path() /
                      "resources" / "assets";
  std::error_code error;
  if (!std::filesystem::is_directory(assets, error) || error) return {};
  for (std::filesystem::directory_iterator iterator(assets, error), end;
       iterator != end && !error; iterator.increment(error)) {
    if (!iterator->is_regular_file(error) || error) continue;
    const auto name = iterator->path().filename().wstring();
    const auto extension = iterator->path().extension().wstring();
    if ((extension == L".mp4" || extension == L".MP4") &&
        name.rfind(L"aurora-4k-sample", 0) == 0)
      return iterator->path();
  }
#endif
  return {};
}

void demo_bridge_adapter::run_media_job(media_job* job) {
  if (!job) return;
  if (!tasks_.mark_running(job->task_id)) {
    if (job->cancel_requested.load()) (void)tasks_.confirm_cancelled(job->task_id, "任务启动前已取消。");
    return;
  }
  emit_media_event(*job, "running", 0, "原生媒体任务已开始。");
  media_encode_result result;
  try {
    const std::function<void(unsigned)> progress = [this, job](unsigned value) {
      (void)tasks_.update_progress(job->task_id, value);
      emit_media_event(*job, "running", value, "C++ 后台处理中。");
    };
    if (job->save_destination.empty()) {
      result = encode_media(job->encoder, job->source, job->output, job->format, job->codec,
                            job->quality, job->cancel_requested, progress, job->operation);
    } else {
      result = save_media_output(job->source, job->output, job->save_destination, job->cancel_requested, progress);
    }
  } catch (const std::exception&) { result = {false, false, "原生媒体任务异常，未完成输出已清理。"}; }
  if (job->cancel_requested.load() && (job->save_destination.empty() || !result.succeeded)) {
    result.succeeded = false; result.cancelled = true; result.message = "任务已取消，未完成输出已删除。";
  }
  if (!result.succeeded) {
    std::error_code ignored;
    std::filesystem::remove(job->output, ignored);
  }
  if (result.cancelled) {
    (void)tasks_.request_cancel(job->task_id);
    (void)tasks_.confirm_cancelled(job->task_id, result.message);
  } else {
    (void)tasks_.complete(job->task_id, result.succeeded ? soui::runtime::result_kind::success : soui::runtime::result_kind::failure, result.message);
  }
  const auto actual = tasks_.query(job->task_id);
  if (actual) emit_media_event(*job, state_name(actual->state), actual->progress, result.message);
}

void demo_bridge_adapter::run_workload_job(workload_job* job) {
  if (job == nullptr || !tasks_.mark_running(job->task_id)) return;
  emit_workload_event(*job, "running", 0, "Native workload started.");
  const auto started = std::chrono::steady_clock::now();
  const auto cancel = [this, job](const std::string& message) {
    (void)tasks_.confirm_cancelled(job->task_id, message);
    emit_workload_event(*job, "cancelled", 0, message);
  };
  const auto fail = [this, job](const std::string& message) {
    (void)tasks_.complete(job->task_id, soui::runtime::result_kind::failure, message);
    emit_workload_event(*job, "failed", 0, message);
  };
  const auto elapsed_ms = [&started] {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  };
  const auto publish_metrics = [this, job](const json& metrics) {
    std::lock_guard<std::mutex> lock(workload_mutex_);
    const auto found = workload_jobs_.find(job->task_id);
    if (found != workload_jobs_.end()) found->second->metrics_json = metrics.dump();
  };

  if (job->kind == "video4k") {
    const std::size_t frames = std::min<std::size_t>(std::max<std::size_t>(job->complexity, 1U), 6U);
    constexpr std::size_t width = 3840;
    constexpr std::size_t height = 2160;
    constexpr std::size_t pixels = width * height;
    std::vector<std::uint32_t> frame(pixels);
    if (frame.empty()) {
      fail("The bounded 4K frame buffer could not be allocated.");
      return;
    }
    std::uint64_t checksum = 0;
    bool native_frame_decoded = false;
    std::string decoder = "synthetic-bgra";
    std::uint32_t native_frames = 0;
    double native_decode_fps = 0.0;
    const auto sample4k = bundled_4k_sample_mp4();
    if (!sample4k.empty()) {
      const auto native_decode = measure_media_decode(sample4k, static_cast<std::uint32_t>(frames));
      native_frame_decoded = native_decode.supported;
      native_frames = native_decode.frames;
      native_decode_fps = native_decode.decode_fps;
      if (native_frame_decoded) decoder = native_decode.decoder;
    }
    for (std::size_t frame_index = 0; frame_index < frames; ++frame_index) {
      if (job->cancel_requested.load(std::memory_order_relaxed)) {
        cancel("4K preview workload cancelled.");
        return;
      }
      for (std::size_t index = 0; index < pixels; ++index) {
        const std::uint32_t x = static_cast<std::uint32_t>(index % width);
        const std::uint32_t y = static_cast<std::uint32_t>(index / width);
        frame[index] = ((x + frame_index * 7U) & 0xFFU) |
                       (((y + frame_index * 3U) & 0xFFU) << 8U) |
                       (((x + y + frame_index * 11U) & 0xFFU) << 16U) | 0xFF000000U;
        checksum += frame[index];
      }
      const auto progress = static_cast<unsigned>(((frame_index + 1U) * 100U) / frames);
      (void)tasks_.update_progress(job->task_id, progress);
      emit_workload_event(*job, "running", progress, "Generating bounded 4K preview frames.");
    }
    const double seconds = std::max(0.001, elapsed_ms() / 1000.0);
    const json metrics{{"kind", "video4k"}, {"width", width}, {"height", height},
                       {"frames", frames}, {"bytesPerFrame", pixels * sizeof(std::uint32_t)},
                       {"throughputMBps", (frames * pixels * sizeof(std::uint32_t)) / seconds / 1000000.0},
                       {"checksum", checksum}, {"decoder", decoder},
                       {"nativeFrameDecoded", native_frame_decoded}, {"nativeFrames", native_frames},
                       {"nativeDecodeFps", native_decode_fps}, {"hardwareDecode", false},
                       {"note", native_frame_decoded ? "4K asset first frame decoded by Media Foundation; continuous playback remains a separate adapter." : "UI pipeline benchmark; native decoder asset unavailable, synthetic BGRA fallback used."}};
    publish_metrics(metrics);
    (void)tasks_.complete(job->task_id, soui::runtime::result_kind::success,
                          "Bounded 4K preview pipeline completed.");
    emit_workload_event(*job, "succeeded", 100, "Bounded 4K preview pipeline completed.");
    return;
  }

  if (job->kind == "cad") {
    const std::size_t entities = std::min<std::size_t>(std::max<std::size_t>(job->complexity, 1000U), 1000000U);
    std::vector<float> scene(entities * 4U);
    constexpr std::size_t bucket_side = 64U;
    constexpr float bucket_size = 32.0F;
    std::vector<std::uint32_t> spatial_buckets(bucket_side * bucket_side, 0U);
    constexpr float viewport_min_x = -256.0F;
    constexpr float viewport_max_x = 1024.0F;
    constexpr float viewport_min_y = -128.0F;
    constexpr float viewport_max_y = 768.0F;
    std::size_t visible_entities = 0;
    std::size_t lod_high = 0;
    std::size_t lod_low = 0;
    const double angle = 0.37;
    const float cosine = static_cast<float>(std::cos(angle));
    const float sine = static_cast<float>(std::sin(angle));
    double checksum = 0.0;
    for (std::size_t index = 0; index < entities; ++index) {
      if ((index & 0x3FFFU) == 0U && job->cancel_requested.load(std::memory_order_relaxed)) {
        cancel("CAD scene workload cancelled.");
        return;
      }
      const float x = static_cast<float>(index % 4096U) * 0.5F;
      const float y = static_cast<float>(index / 4096U) * 0.5F;
      const float transformed_x = x * cosine - y * sine;
      const float transformed_y = x * sine + y * cosine;
      scene[index * 4U] = transformed_x;
      scene[index * 4U + 1U] = transformed_y;
      scene[index * 4U + 2U] = x;
      scene[index * 4U + 3U] = y;
      const auto bucket_x = static_cast<int>(std::floor(transformed_x / bucket_size)) + 32;
      const auto bucket_y = static_cast<int>(std::floor(transformed_y / bucket_size)) + 32;
      if (bucket_x >= 0 && bucket_x < static_cast<int>(bucket_side) &&
          bucket_y >= 0 && bucket_y < static_cast<int>(bucket_side))
        ++spatial_buckets[static_cast<std::size_t>(bucket_y) * bucket_side + static_cast<std::size_t>(bucket_x)];
      if (transformed_x >= viewport_min_x && transformed_x <= viewport_max_x &&
          transformed_y >= viewport_min_y && transformed_y <= viewport_max_y) {
        ++visible_entities;
        if ((index & 3U) == 0U) ++lod_high;
        else ++lod_low;
      }
      checksum += transformed_x + transformed_y;
      if ((index % std::max<std::size_t>(1U, entities / 20U)) == 0U) {
        const auto progress = static_cast<unsigned>((index * 100U) / entities);
        (void)tasks_.update_progress(job->task_id, progress);
        emit_workload_event(*job, "running", progress, "Transforming native CAD scene." );
      }
    }
    const double seconds = std::max(0.001, elapsed_ms() / 1000.0);
    const json metrics{{"kind", "cad"}, {"entities", entities}, {"transformed", entities},
                       {"visibleEntities", visible_entities}, {"culledEntities", entities - visible_entities},
                       {"lodHigh", lod_high}, {"lodLow", lod_low}, {"spatialBuckets", bucket_side * bucket_side},
                       {"entitiesPerSecond", entities / seconds}, {"checksum", checksum},
                       {"geometryKernel", "bounded-native-transform-grid-cull"},
                       {"note", "Viewport/scene pipeline benchmark with spatial culling and LOD buckets; not a CAD kernel"}};
    publish_metrics(metrics);
    (void)tasks_.complete(job->task_id, soui::runtime::result_kind::success,
                          "Native CAD scene transform completed.");
    emit_workload_event(*job, "succeeded", 100, "Native CAD scene transform completed.");
    return;
  }

  if (job->kind == "zip") {
    const std::size_t entries = std::min<std::size_t>(std::max<std::size_t>(job->complexity, 1U), 256U);
    constexpr std::size_t bytes_per_entry = 16U * 1024U;
    std::vector<std::uint8_t> archive;
    archive.reserve(entries * (bytes_per_entry + 64U) + 1024U);
    for (std::size_t index = 0; index < entries; ++index) {
      if (job->cancel_requested.load(std::memory_order_relaxed)) {
        cancel("ZIP workload cancelled.");
        return;
      }
      std::vector<std::uint8_t> payload(bytes_per_entry, static_cast<std::uint8_t>(index & 0xFFU));
      // Repeat a small pattern so the generated archive exercises the real
      // DEFLATE path instead of merely storing incompressible bytes.
      for (std::size_t byte = 0; byte < payload.size(); ++byte)
        payload[byte] = static_cast<std::uint8_t>((byte / 64U + index) & 0x0FU);
      uLong compressed_bound = compressBound(static_cast<uLong>(payload.size()));
      std::vector<std::uint8_t> compressed(compressed_bound);
      uLong compressed_size = compressed_bound;
      if (compress2(compressed.data(), &compressed_size, payload.data(),
                    static_cast<uLong>(payload.size()), Z_BEST_SPEED) != Z_OK) {
        fail("Native zlib could not create the bounded deflate entry.");
        return;
      }
      const auto name = std::string("entry-") + std::to_string(index) + ".bin";
      append_u32(archive, 0x04034B50U);
      append_u16(archive, 20); append_u16(archive, 0); append_u16(archive, 8);
      append_u16(archive, 0); append_u16(archive, 0);
      append_u32(archive, crc32_bytes(payload.data(), payload.size()));
      append_u32(archive, static_cast<std::uint32_t>(compressed_size));
      append_u32(archive, static_cast<std::uint32_t>(payload.size()));
      append_u16(archive, static_cast<std::uint16_t>(name.size())); append_u16(archive, 0);
      archive.insert(archive.end(), name.begin(), name.end());
      archive.insert(archive.end(), compressed.begin(), compressed.begin() + compressed_size);
      if ((index % std::max<std::size_t>(1U, entries / 20U)) == 0U) {
        const auto progress = static_cast<unsigned>((index * 100U) / entries);
        (void)tasks_.update_progress(job->task_id, progress);
        emit_workload_event(*job, "running", progress, "Streaming stored ZIP entries." );
      }
    }
    std::size_t cursor = 0;
    std::size_t verified = 0;
    std::uintmax_t bytes = 0;
    while (cursor + 30U <= archive.size() && read_u32(archive, cursor) == 0x04034B50U) {
      const auto compressed = read_u32(archive, cursor + 18U);
      const auto uncompressed = read_u32(archive, cursor + 22U);
      const auto method = read_u16(archive, cursor + 8U);
      const auto name_length = read_u16(archive, cursor + 26U);
      const auto extra_length = read_u16(archive, cursor + 28U);
      const auto payload_offset = cursor + 30U + name_length + extra_length;
      if ((method != 0U && method != 8U) || payload_offset + compressed > archive.size() ||
          uncompressed > 4U * 1024U * 1024U) {
        fail("The bounded ZIP parser encountered an unsupported or oversized entry.");
        return;
      }
      const auto expected_crc = read_u32(archive, cursor + 14U);
      std::vector<std::uint8_t> decoded(uncompressed);
      if (method == 0U) {
        if (compressed != uncompressed) {
          fail("The bounded ZIP stored entry has mismatched sizes.");
          return;
        }
        std::copy_n(archive.data() + payload_offset, uncompressed, decoded.data());
      } else {
        uLong decoded_size = static_cast<uLong>(decoded.size());
        if (uncompress(decoded.data(), &decoded_size, archive.data() + payload_offset,
                       static_cast<uLong>(compressed)) != Z_OK || decoded_size != decoded.size()) {
          fail("Native zlib could not inflate the bounded ZIP entry.");
          return;
        }
      }
      if (crc32_bytes(decoded.data(), decoded.size()) != expected_crc) {
        fail("ZIP CRC verification failed.");
        return;
      }
      ++verified;
      bytes += decoded.size();
      cursor = payload_offset + compressed;
    }
    const double seconds = std::max(0.001, elapsed_ms() / 1000.0);
    const json metrics{{"kind", "zip"}, {"entries", entries}, {"verifiedEntries", verified},
                       {"bytes", bytes}, {"throughputMBps", bytes / seconds / 1000000.0},
                       {"compression", "deflate"}, {"zipBombGuard", true},
                       {"note", "Streaming bounded ZIP deflate integrity benchmark; ZIP64/7z remain separate adapters"}};
    publish_metrics(metrics);
    (void)tasks_.complete(job->task_id, soui::runtime::result_kind::success,
                          "Streaming ZIP integrity workload completed.");
    emit_workload_event(*job, "succeeded", 100, "Streaming ZIP integrity workload completed.");
    return;
  }

  fail("Unknown workload kind.");
}

void demo_bridge_adapter::emit_workload_event(const workload_job& job, const std::string& state,
                                              unsigned progress, const std::string& message) const {
  event_sink sink_copy;
  std::uint64_t sequence = 0;
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    const bool subscribed = std::any_of(subscriptions_.begin(), subscriptions_.end(), [&](const auto& entry) {
      const auto& binding = entry.second;
      return binding.context.session_id == job.context.session_id && binding.task_id == job.task_id &&
             binding.event_name == "task.progress";
    });
    if (!subscribed) return;
    sink_copy = event_sink_;
    sequence = next_media_event_sequence_++;
  }
  if (!sink_copy) return;
  sink_copy(json{{"sessionId", job.context.session_id}, {"sequence", sequence},
                 {"eventName", "task.progress"}, {"taskId", job.task_id},
                 {"payload", {{"state", state}, {"progress", progress}, {"message", message},
                               {"kind", job.kind}, {"indeterminate", state == "accepted" || state == "running" ||
                                                                  state == "cancel-pending"}}}}
                 .dump());
}

void demo_bridge_adapter::join_finished_workload_jobs() {
  std::vector<std::unique_ptr<workload_job>> finished;
  {
    std::lock_guard<std::mutex> lock(workload_mutex_);
    for (auto iterator = workload_jobs_.begin(); iterator != workload_jobs_.end();) {
      const auto task = tasks_.query(iterator->first);
      if (task.has_value() && terminal_task_state(task->state)) {
        finished.push_back(std::move(iterator->second));
        iterator = workload_jobs_.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  for (auto& job : finished) if (job && job->worker.joinable()) job->worker.join();
}

void demo_bridge_adapter::shutdown_workload_jobs(const std::string& session_id) {
  std::vector<std::unique_ptr<workload_job>> jobs;
  {
    std::lock_guard<std::mutex> lock(workload_mutex_);
    for (auto iterator = workload_jobs_.begin(); iterator != workload_jobs_.end();) {
      if (session_id.empty() || iterator->second->context.session_id == session_id) {
        iterator->second->cancel_requested.store(true, std::memory_order_relaxed);
        jobs.push_back(std::move(iterator->second));
        iterator = workload_jobs_.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  for (auto& job : jobs) if (job && job->worker.joinable()) job->worker.join();
}

void demo_bridge_adapter::emit_media_event(const media_job& job, const std::string& state,
                                           unsigned progress, const std::string& message) const {
  event_sink sink_copy;
  std::uint64_t sequence = 0;
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    const bool subscribed = std::any_of(subscriptions_.begin(), subscriptions_.end(), [&](const auto& entry) {
      const auto& binding = entry.second;
      return binding.context.session_id == job.context.session_id && binding.task_id == job.task_id &&
             binding.event_name == "task.progress";
    });
    if (!subscribed) return;
    sink_copy = event_sink_;
    sequence = next_media_event_sequence_++;
  }
  if (!sink_copy) return;
  sink_copy(json{{"sessionId", job.context.session_id},
                 {"sequence", sequence},
                 {"eventName", "task.progress"},
                 {"taskId", job.task_id},
                 {"payload", {{"state", state}, {"progress", progress}, {"message", message},
                               {"indeterminate", state == "accepted" || state == "running" ||
                                                     state == "cancel-pending"}}}}
                 .dump());
}

void demo_bridge_adapter::join_finished_media_jobs() {
  std::vector<std::unique_ptr<media_job>> finished;
  {
    std::lock_guard<std::mutex> lock(media_mutex_);
    for (auto iterator = media_jobs_.begin(); iterator != media_jobs_.end();) {
      const auto task = tasks_.query(iterator->first);
      if (task.has_value() && terminal_task_state(task->state)) {
        finished.push_back(std::move(iterator->second));
        iterator = media_jobs_.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  for (auto& job : finished) {
    if (job && job->worker.joinable()) job->worker.join();
  }
}

void demo_bridge_adapter::shutdown_media_jobs(const std::string& session_id) {
  std::vector<std::unique_ptr<media_job>> jobs;
  {
    std::lock_guard<std::mutex> lock(media_mutex_);
    for (auto iterator = media_jobs_.begin(); iterator != media_jobs_.end();) {
      if (session_id.empty() || iterator->second->context.session_id == session_id) {
        iterator->second->cancel_requested.store(true, std::memory_order_relaxed);
        jobs.push_back(std::move(iterator->second));
        iterator = media_jobs_.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  for (auto& job : jobs) {
    if (job && job->worker.joinable()) job->worker.join();
  }
  for (const auto& job : jobs) {
    std::error_code ignored;
    std::filesystem::remove(job->output, ignored);
    if (job->save_destination.empty()) std::filesystem::remove(job->output.parent_path(), ignored);
    (void)tasks_.erase(job->task_id);
  }
}

void demo_bridge_adapter::shutdown_media_files(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(media_mutex_);
  for (auto iterator = media_files_.begin(); iterator != media_files_.end();) {
    if (session_id.empty() || iterator->second.context.session_id == session_id) {
      iterator = media_files_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

std::string demo_bridge_adapter::start_media_job(
    const soui::runtime::scan_request_context& context, const std::string& source,
    const std::string& format, const std::string& codec, unsigned quality,
    const std::string& operation, const std::string& operation_id) {
  if (!valid_media_operation(operation)) {
    return payload_error("E_INVALID_PARAMS", "Unsupported native media operation.");
  }
  if (operation == "rotate90" || operation == "mirror") {
    if (format != "MP4" && format != "MOV" && format != "WebM" && format != "PNG" && format != "WebP") {
      return payload_error("E_INVALID_PARAMS", "Rotation and mirror require a video or image output.");
    }
  }
  if ((operation == "reverse" || operation == "speed2x") &&
      format != "MP4" && format != "MOV" && format != "WebM" && format != "MP3" && format != "WAV") {
    return payload_error("E_INVALID_PARAMS", "Reverse and speed require a video or audio output.");
  }
  if (operation == "crop16x9" && format != "MP4" && format != "MOV" && format != "WebM" &&
      format != "PNG" && format != "WebP") {
    return payload_error("E_INVALID_PARAMS", "Crop requires a video or image output.");
  }
  if (operation == "trim10" && format != "MP4" && format != "MOV" && format != "WebM" &&
      format != "MP3" && format != "WAV") {
    return payload_error("E_INVALID_PARAMS", "Segment extraction requires a video or audio output.");
  }
  if ((operation == "fade" || operation == "grayscale" || operation == "watermark") &&
      format != "MP4" && format != "MOV" && format != "WebM" && format != "PNG" && format != "WebP") {
    return payload_error("E_INVALID_PARAMS", "This visual operation requires a video or image output.");
  }
  if ((operation == "denoise" || operation == "normalize") &&
      format != "MP4" && format != "MOV" && format != "WebM" && format != "MP3" && format != "WAV") {
    return payload_error("E_INVALID_PARAMS", "This audio operation requires a video or audio output.");
  }
  if (!valid_media_profile(format, codec, quality)) {
    return payload_error("E_INVALID_PARAMS", "Unsupported format/codec or quality outside 10..95.");
  }
  const auto encoder = configured_media_encoder();
  const bool native_png_snapshot = operation.empty() && encoder.empty() && format == "PNG" &&
                                   codec == "PNG" && media_preview_available();
  if (encoder.empty() && !native_png_snapshot) {
    return payload_error("E_UNIMPLEMENTED", "请配置 SOUI_FFMPEG_PATH；未配置时不会把复制文件冒充转换。PNG 可使用内置 Media Foundation 快照。");
  }
  if (operation_id.empty()) return payload_error("E_INVALID_PARAMS", "operationId is required.");

  std::filesystem::path source_path;
  if (source == "sample") {
    source_path = bundled_sample_mp4();
    if (source_path.empty()) return payload_error("E_RESOURCE_MISSING", "The bundled sample MP4 is missing.");
  } else {
    std::lock_guard<std::mutex> lock(media_mutex_);
    const auto found = media_files_.find(source);
    if (found == media_files_.end() || found->second.context.session_id != context.session_id ||
        found->second.context.origin != context.origin || found->second.context.token != context.token) {
      return payload_error("E_TASK_NOT_FOUND", "The media file token is not valid for this session.");
    }
    source_path = found->second.path;
  }
  std::error_code source_error;
  if (!std::filesystem::is_regular_file(source_path, source_error) || source_error) {
    return payload_error("E_RESOURCE_MISSING", "The selected media file is no longer available.");
  }

  bool duplicate = false;
  bool conflict = false;
  bool capacity = false;
  const auto scope = context.session_id + "|" + context.origin + "|media|" + source + "|" +
                     operation + "|" + format + "|" + codec + "|" + std::to_string(quality);
  const auto snapshot = tasks_.start_scoped(operation_id, scope, &duplicate, &conflict, &capacity);
  if (conflict) return payload_error("E_OPERATION_CONFLICT", "operationId is already used by another media request.");
  if (capacity) return payload_error("E_BUSY", "The native task store capacity is exhausted.");
  if (duplicate) {
    const auto existing = json::parse(media_task_json(context, snapshot.task_id), nullptr, false);
    if (!existing.is_object() || existing.value("error", "") != "") {
      return payload_error("E_TASK_NOT_FOUND", "The duplicate media task is no longer retained.");
    }
    return json{{"taskId", snapshot.task_id}, {"accepted", true},
                {"output", existing.value("output", "")}}.dump();
  }

  {
    std::lock_guard<std::mutex> lock(media_mutex_);
    std::size_t running = 0;
    for (const auto& item : media_jobs_) {
      const auto state = tasks_.query(item.first);
      if (state && !terminal_task_state(state->state)) ++running;
    }
    if (running >= 2 || media_jobs_.size() >= 32) {
      (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure, "Media capacity reached.");
      (void)tasks_.erase(snapshot.task_id);
      return payload_error("E_BUSY", "最多同时处理 2 个任务，当前会话最多保留 32 个结果。");
    }
  }

  std::error_code error;
  auto output_directory = std::filesystem::temp_directory_path(error);
  if (error || output_directory.empty()) output_directory = std::filesystem::current_path(error);
  if (error || output_directory.empty()) {
    (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure,
                          "A temporary output directory could not be resolved.");
    (void)tasks_.erase(snapshot.task_id);
    return payload_error("E_NATIVE_FAILURE", "A temporary output directory could not be resolved.");
  }
  auto job = std::make_unique<media_job>();
  job->context = context;
  job->task_id = snapshot.task_id;
  job->source = std::move(source_path);
  GUID output_id{};
  if (FAILED(CoCreateGuid(&output_id))) {
    (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure, "Output allocation failed.");
    (void)tasks_.erase(snapshot.task_id);
    return payload_error("E_NATIVE_FAILURE", "Could not allocate output identity.");
  }
  wchar_t output_suffix[40]{};
  StringFromGUID2(output_id, output_suffix, 40);
  output_directory /= std::wstring(L"soui-media-") + output_suffix;
  if (!std::filesystem::create_directory(output_directory, error) || error) {
    (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure, "Output directory failed.");
    (void)tasks_.erase(snapshot.task_id);
    return payload_error("E_NATIVE_FAILURE", "Could not create isolated output directory.");
  }
  job->output = output_directory / ("result" + media_output_extension(format));
  job->encoder = encoder;
  job->format = format;
  job->codec = codec;
  job->operation = operation;
  job->quality = quality;
  const auto output_name = job->output.filename().generic_string();
  media_job* raw = job.get();
  {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (media_jobs_.size() >= 32U) {
      std::error_code ignored;
      std::filesystem::remove_all(output_directory, ignored);
      (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure, "Media capacity reached.");
      (void)tasks_.erase(snapshot.task_id);
      return payload_error("E_BUSY", "Too many retained media tasks; query or restart the session first.");
    }
    media_jobs_.emplace(snapshot.task_id, std::move(job));
  }
  try {
    raw->worker = std::thread([this, raw] { run_media_job(raw); });
  } catch (...) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    media_jobs_.erase(snapshot.task_id);
    std::error_code ignored;
    std::filesystem::remove_all(output_directory, ignored);
    (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure,
                          "The native media worker could not be started.");
    (void)tasks_.erase(snapshot.task_id);
    return payload_error("E_NATIVE_FAILURE", "The native media worker could not be started.");
  }
  return json{{"taskId", snapshot.task_id}, {"accepted", true}, {"output", output_name}}.dump();
}

void demo_bridge_adapter::register_methods() {
  (void)registry_.register_method("desktop.rendering.setTargetRate", "desktop.rendering.control",
      [this](std::string_view params) -> std::string {
        const auto rate = size_param(std::string(params), "targetHz", 0);
        if (rate != 60 && rate != 120 && rate != 144) return payload_error("E_INVALID_PARAMS", "Allowed targets: 60, 120, 144.");
        preferred_frame_rate_.store(static_cast<unsigned>(rate));
        requested_frame_rate_.store(static_cast<unsigned>(rate));
        return json{{"requestedHz", rate}}.dump();
      });
  (void)registry_.register_method("desktop.rendering.getStats", "bridge.probe",
      [this](std::string_view) -> std::string {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const char* gpu = std::getenv("SOUI_GPU_COMPOSITOR");
        const char* direct = std::getenv("SOUI_GPU_DIRECT_PRESENT");
        const char* diagnostic = std::getenv("SOUI_GPU_DIAGNOSTIC_READBACK");
        const char* active = std::getenv("SOUI_GPU_ACTIVE");
        bool processActive = false;
        bool processDirect = false;
        unsigned windowDpi = 96;
#ifdef _WIN32
        char processActiveOption[2] = {};
        char processDirectOption[2] = {};
        processActive = GetEnvironmentVariableA("SOUI_GPU_ACTIVE", processActiveOption,
                                                sizeof(processActiveOption)) == 1 && processActiveOption[0] == '1';
        processDirect = GetEnvironmentVariableA("SOUI_GPU_DIRECT_PRESENT", processDirectOption,
                                                sizeof(processDirectOption)) == 1 && processDirectOption[0] == '1';
        if (owner_window_ != nullptr) {
          using get_dpi_for_window = UINT(WINAPI*)(HWND);
          const auto user32 = GetModuleHandleW(L"user32.dll");
          const auto get_dpi = user32 == nullptr
              ? nullptr
              : reinterpret_cast<get_dpi_for_window>(GetProcAddress(user32, "GetDpiForWindow"));
          if (get_dpi != nullptr) {
            const UINT dpi = get_dpi(static_cast<HWND>(owner_window_));
            if (dpi > 0) windowDpi = dpi;
          }
        }
#endif
        const bool isActive = gpu_active_.load(std::memory_order_relaxed) || processActive || (active && active[0] == '1');
        const bool isDirect = processDirect || (direct && direct[0] == '1');
        // The bridge is built with its own CRT, while the experimental kernel
        // DLL owns the live SOUI_GPU_ACTIVE flag in a separate CRT image. Keep
        // the requested mode visible even before the compositor has published
        // its active bit; a real active value still wins when it is available.
        const std::string renderer = isActive
                                         ? (isDirect ? "gpu-direct-active" : "gpu-readback-active")
                                         : (gpu && gpu[0] == '1'
                                                ? (isDirect ? "gpu-direct-requested" : "gpu-readback-requested")
                                                : "software-skia-gdi");
        return json{{"paintCount", paint_count_.load(std::memory_order_relaxed)},
                    {"presentCount", present_count_.load(std::memory_order_relaxed)},
                    {"displayRefreshHz", display_refresh_.load(std::memory_order_relaxed)},
                    {"sampledAtMs", std::chrono::duration<double, std::milli>(now).count()},
                    {"gpuStatus", renderer},
                    {"rendererBackend", renderer},
                    {"diagnosticReadback", diagnostic && diagnostic[0] == '1'},
                    {"architecture", sizeof(void*) == 8 ? "x64" : "x86"},
                    {"windowDpi", windowDpi},
                    {"deviceScaleFactor", static_cast<double>(windowDpi) / 96.0},
                    {"requestedHz", requested_frame_rate_.load()},
                    {"activePacingHz", active_frame_rate_.load()},
                    {"drawIntervalMs", draw_interval_ms_.load(std::memory_order_relaxed)}}.dump();
      });
  (void)registry_.register_method("desktop.media.getCapabilities", "bridge.probe",
      [this](std::string_view) -> std::string {
        const auto sample = bundled_sample_mp4();
        const bool ffmpeg = !configured_media_encoder().empty();
        const bool media_foundation = media_preview_available();
        return json{{"samplePassthrough", false},
                    {"nativeDecoder", media_foundation},
                    {"nativeStillImage", media_foundation},
                    {"ffmpeg", ffmpeg},
                    {"note", ffmpeg ? "FFmpeg executable configured; encoder support is verified when the task runs."
                                     : media_foundation ? "Media Foundation first-frame preview is available; FFmpeg transcoding is not configured."
                                                         : "No media engine configured; conversion is unavailable."}}
            .dump();
      });
  (void)registry_.register_method(
      "desktop.media.preview.start", "media.convert", [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto source = string_param(value, "source");
        const auto operation = string_param(value, "operationId");
        if (source.empty() || operation.empty()) {
          return payload_error("E_INVALID_PARAMS", "source and operationId are required.");
        }
        std::filesystem::path source_path;
        if (source == "sample") {
          source_path = bundled_sample_mp4();
        } else {
          std::lock_guard<std::mutex> lock(media_mutex_);
          const auto found = media_files_.find(source);
          if (found == media_files_.end() || found->second.context.session_id != context.session_id ||
              found->second.context.origin != context.origin || found->second.context.token != context.token) {
            return payload_error("E_TASK_NOT_FOUND", "The media file token is not valid for this session.");
          }
          source_path = found->second.path;
        }
        if (source_path.empty() || !std::filesystem::is_regular_file(source_path)) {
          return payload_error("E_RESOURCE_MISSING", "The media preview source is missing.");
        }
        join_finished_media_preview_jobs();
        bool duplicate = false;
        bool conflict = false;
        bool capacity = false;
        const auto snapshot = tasks_.start_scoped(operation, context.session_id + "|" + context.origin + "|media-preview",
                                                  &duplicate, &conflict, &capacity);
        if (conflict) return payload_error("E_OPERATION_CONFLICT", "operationId is already used for another preview.");
        if (capacity) return payload_error("E_BUSY", "The native task store capacity is exhausted.");
        if (duplicate) return json{{"taskId", snapshot.task_id}, {"accepted", true}}.dump();
        auto job = std::make_unique<media_preview_job>();
        job->context = context;
        job->task_id = snapshot.task_id;
        job->source = std::move(source_path);
        media_preview_job* raw = job.get();
        {
          std::lock_guard<std::mutex> lock(media_preview_mutex_);
          if (media_preview_jobs_.size() >= 8U) {
            (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure,
                                  "Media preview capacity reached.");
            (void)tasks_.erase(snapshot.task_id);
            return payload_error("E_BUSY", "Too many retained media preview tasks.");
          }
          media_preview_jobs_.emplace(snapshot.task_id, std::move(job));
        }
        try {
          raw->worker = std::thread([this, raw] { run_media_preview_job(raw); });
        } catch (...) {
          std::lock_guard<std::mutex> lock(media_preview_mutex_);
          media_preview_jobs_.erase(snapshot.task_id);
          (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure,
                                "The native media preview worker could not be started.");
          return payload_error("E_NATIVE_FAILURE", "The native media preview worker could not be started.");
        }
        return json{{"taskId", snapshot.task_id}, {"accepted", true}}.dump();
      });
  (void)registry_.register_method(
      "desktop.media.preview.query", "media.convert", [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto task_id = string_param(value, "taskId");
        if (task_id.empty()) return payload_error("E_INVALID_PARAMS", "taskId is required.");
        return media_preview_task_json(context, task_id);
      });
  (void)registry_.register_method("desktop.workload.getCapabilities", "bridge.probe",
      [this](std::string_view) -> std::string {
        (void)this;
        return json{{"video4kSynthetic", true}, {"video4kNativeFrame", !bundled_4k_sample_mp4().empty()},
                    {"cadScene", true}, {"zipStored", true}, {"zipDeflate", true},
                    {"hardwareDecode", false},
                    {"note", "Native bounded workload probes run outside the WebView; zlib deflate/CRC is available, while hardware decode, CAD geometry kernels, ZIP64 and 7z remain separate adapters."}}.dump();
      });
  (void)registry_.register_method("desktop.media.saveOutput", "media.convert",
      [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        if (!bool_param(value, "userGesture")) return payload_error("E_GESTURE_REQUIRED", "Saving requires a native user gesture.");
        const auto source_task = string_param(value, "taskId");
        std::filesystem::path source;
        {
          std::lock_guard<std::mutex> lock(media_mutex_);
          const auto found = media_jobs_.find(source_task);
          if (found == media_jobs_.end() || found->second->context.session_id != context.session_id ||
              found->second->context.origin != context.origin || found->second->context.token != context.token ||
              !found->second->save_destination.empty()) return payload_error("E_TASK_NOT_FOUND", "Output is not owned by this session.");
          const auto task = tasks_.query(source_task);
          if (!task || task->state != soui::runtime::task_state::succeeded) return payload_error("E_BUSY", "Output is not complete.");
          if (media_jobs_.size() >= 32) return payload_error("E_BUSY", "Session output limit reached.");
          source = found->second->output;
        }
        wchar_t name[32768]{};
        const auto default_name = L"processed" + source.extension().wstring();
        std::copy(default_name.begin(), default_name.end(), name);
        const auto extension = source.extension().wstring().substr(1);
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = static_cast<HWND>(owner_window_);
        dialog.lpstrFile = name; dialog.nMaxFile = 32768; dialog.lpstrDefExt = extension.c_str();
        dialog.lpstrFilter = L"Media output\0*.*\0\0";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog)) {
          if (!CommDlgExtendedError()) return R"({"cancelled":true,"taskId":"","name":""})";
          return payload_error("E_NATIVE_FAILURE", "Save dialog failed.");
        }
        std::error_code error;
        auto destination = std::filesystem::weakly_canonical(std::filesystem::path(name), error);
        if (error || destination.empty()) return payload_error("E_INVALID_PARAMS", "Invalid save destination.");
        {
          std::lock_guard<std::mutex> lock(media_mutex_);
          for (const auto& file : media_files_) {
            if (destination == file.second.path || (std::filesystem::exists(destination, error) && std::filesystem::equivalent(destination, file.second.path, error))) return payload_error("E_INVALID_PARAMS", "不能覆盖已选择的源文件，请使用新文件名。");
          }
          for (const auto& job : media_jobs_) {
            if (destination == job.second->source || destination == job.second->output || destination == job.second->save_destination) return payload_error("E_INVALID_PARAMS", "Choose a different destination; this file is owned by an active task.");
          }
        }
        GUID id{}; if (FAILED(CoCreateGuid(&id))) return payload_error("E_NATIVE_FAILURE", "Could not allocate save task.");
        wchar_t suffix[40]{}; StringFromGUID2(id, suffix, 40);
        const auto operation = wide_to_utf8(suffix);
        bool capacity = false;
        bool conflict = false;
        const auto task = tasks_.start_scoped(operation, context.session_id + "|media-save",
                                             nullptr, &conflict, &capacity);
        if (conflict) return payload_error("E_OPERATION_CONFLICT", "The generated save operationId is already in use.");
        if (capacity) return payload_error("E_BUSY", "The native task store capacity is exhausted.");
        auto job = std::make_unique<media_job>();
        job->context = context; job->task_id = task.task_id; job->source = source;
        job->save_destination = destination;
        job->output = destination.parent_path() / (std::wstring(L".soui-save-") + suffix + L".tmp");
        auto* raw = job.get();
        {
          std::lock_guard<std::mutex> lock(media_mutex_);
          if (media_jobs_.size() >= 32U) {
            (void)tasks_.complete(task.task_id, soui::runtime::result_kind::failure, "Media capacity reached.");
            (void)tasks_.erase(task.task_id);
            return payload_error("E_BUSY", "Too many retained media tasks; query or restart the session first.");
          }
          media_jobs_.emplace(task.task_id, std::move(job));
        }
        try { raw->worker = std::thread([this, raw] { run_media_job(raw); }); }
        catch (...) {
          std::lock_guard<std::mutex> lock(media_mutex_); media_jobs_.erase(task.task_id);
          (void)tasks_.complete(task.task_id, soui::runtime::result_kind::failure,
                                "The native media save worker could not be started.");
          (void)tasks_.erase(task.task_id);
          return payload_error("E_NATIVE_FAILURE", "Could not start save worker.");
        }
        return json{{"cancelled", false}, {"taskId", task.task_id}, {"name", wide_to_utf8(destination.filename().wstring())}}.dump();
      });
  (void)registry_.register_method("desktop.media.pickFile", "desktop.media.pickFile",
      [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::desktop_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token"),
            bool_param(value, "userGesture")};
        if (!context.user_gesture) return payload_error("E_GESTURE_REQUIRED", "Choosing a media file requires a user gesture.");
#ifdef _WIN32
        wchar_t file_name[MAX_PATH]{};
        constexpr wchar_t filter[] = L"Media files\0*.mp4;*.mov;*.webm;*.mkv;*.avi;*.mp3;*.wav;*.flac;*.ogg;*.png;*.jpg;*.jpeg;*.webp\0\0";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = static_cast<HWND>(owner_window_);
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = file_name;
        dialog.nMaxFile = static_cast<DWORD>(std::size(file_name));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) {
          if (CommDlgExtendedError() == 0) return R"({"cancelled":true,"fileToken":"","name":"","size":0})";
          return payload_error("E_NATIVE_FAILURE", "The native media file picker failed.");
        }
        std::error_code error;
        const auto path = std::filesystem::weakly_canonical(std::filesystem::path(file_name), error);
        if (error || !std::filesystem::is_regular_file(path, error) || error) {
          return payload_error("E_RESOURCE_MISSING", "The selected media file could not be resolved.");
        }
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        const std::unordered_set<std::wstring> extensions{L".mp4", L".mov", L".webm", L".mkv", L".avi", L".mp3", L".wav", L".flac", L".ogg", L".png", L".jpg", L".jpeg", L".webp"};
        if (!extensions.count(extension)) return payload_error("E_INVALID_PARAMS", "Unsupported local media file type.");
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > (std::uintmax_t{4} * 1024U * 1024U * 1024U)) {
          return payload_error("E_RESULT_LIMIT", "The selected media file is empty or exceeds the 4 GiB demo limit.");
        }
        std::string token;
        {
          std::lock_guard<std::mutex> lock(media_mutex_);
          if (media_files_.size() >= 64) return payload_error("E_BUSY", "Too many selected files; restart this workspace.");
          token = "media-file-" + std::to_string(next_media_file_++);
          media_files_.emplace(token, media_file_binding{
              {context.session_id, context.origin, context.token}, path,
              wide_to_utf8(path.filename().wstring()), size});
        }
        return json{{"cancelled", false}, {"fileToken", token},
                    {"name", wide_to_utf8(path.filename().wstring())}, {"size", size}}.dump();
#else
        return payload_error("E_UNSUPPORTED", "The native media file picker is available on Windows hosts only.");
#endif
      });
#ifdef SOUI_BRIDGE_BENCHMARK
  (void)registry_.register_method("bridge.benchmark.ping", "bridge.probe",
      [](std::string_view) -> std::string { return "{}"; });
#endif
  (void)registry_.register_method(
      "bridge.probe.report", "bridge.probe", [](std::string_view params) -> std::string {
        const std::string value(params);
        for (const char* feature : {"script", "json", "event", "errorHandling", "localResource",
                                    "proxy", "promise", "symbol", "reflect", "vueMount",
                                    "reactivity", "domEvent", "bridge"}) {
          if (!demo_bridge_adapter::bool_param(value, feature)) {
            return payload_error("E_COMPATIBILITY_UNSUPPORTED",
                                 "The embedded Web runtime failed a required capability probe.");
          }
        }
        return R"({"accepted":true})";
      });
  (void)registry_.register_method(
      "media.transform.start", "media.convert", [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        return start_media_job(context, string_param(value, "source"), string_param(value, "format"),
                               string_param(value, "codec"), static_cast<unsigned>(size_param(value, "quality", 82)),
                               string_param(value, "operation"), string_param(value, "operationId"));
      });
  (void)registry_.register_method(
      "media.convert.start", "media.convert", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto source = string_param(value, "source");
        const auto format = string_param(value, "format");
        const auto codec = string_param(value, "codec");
        const auto operation = string_param(value, "operationId");
        const auto quality = size_param(value, "quality", 82);
        if (operation.empty()) return payload_error("E_INVALID_PARAMS", "operationId is required.");
        if (quality > 95 || !valid_media_profile(format, codec, static_cast<unsigned>(quality))) return payload_error("E_INVALID_PARAMS", "Unsupported format/codec or quality outside 10..95.");
        const auto encoder = configured_media_encoder();
        const bool native_png_snapshot = encoder.empty() && format == "PNG" && codec == "PNG" && media_preview_available();
        if (encoder.empty() && !native_png_snapshot) return payload_error("E_UNIMPLEMENTED", "请配置 SOUI_FFMPEG_PATH；未配置时不会把复制文件冒充转换。PNG 可使用内置 Media Foundation 快照。");
        std::filesystem::path source_path;
        if (source == "sample") {
          source_path = bundled_sample_mp4();
          if (source_path.empty()) return payload_error("E_RESOURCE_MISSING", "The bundled sample MP4 is missing.");
        } else {
          std::lock_guard<std::mutex> lock(media_mutex_);
          const auto found = media_files_.find(source);
          if (found == media_files_.end() || found->second.context.session_id != context.session_id ||
              found->second.context.origin != context.origin || found->second.context.token != context.token) {
            return payload_error("E_TASK_NOT_FOUND", "The media file token is not valid for this session.");
          }
          source_path = found->second.path;
        }
        std::error_code source_error;
        if (!std::filesystem::is_regular_file(source_path, source_error) || source_error) {
          return payload_error("E_RESOURCE_MISSING", "The selected media file is no longer available.");
        }
        bool duplicate = false;
        bool conflict = false;
        bool capacity = false;
        const auto snapshot = tasks_.start_scoped(
            operation, context.session_id + "|" + context.origin + "|media|" + source + "|" + format + "|" + codec + "|" + std::to_string(quality), &duplicate, &conflict, &capacity);
        if (conflict) return payload_error("E_OPERATION_CONFLICT", "operationId is already used by another media request.");
        if (capacity) return payload_error("E_BUSY", "The native task store capacity is exhausted.");
        if (duplicate) {
          const auto existing = json::parse(media_task_json(context, snapshot.task_id), nullptr, false);
          if (!existing.is_object() || existing.value("error", "") != "") {
            return payload_error("E_TASK_NOT_FOUND", "The duplicate media task is no longer retained.");
          }
          return json{{"taskId", snapshot.task_id}, {"accepted", true},
                      {"output", existing.value("output", "")}}.dump();
        }
        {
          std::lock_guard<std::mutex> lock(media_mutex_);
          std::size_t running = 0;
          for (const auto& item : media_jobs_) {
            const auto state = tasks_.query(item.first);
            if (state && !terminal_task_state(state->state)) ++running;
          }
          if (running >= 2 || media_jobs_.size() >= 32) {
            (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure, "Media capacity reached.");
            (void)tasks_.erase(snapshot.task_id);
            return payload_error("E_BUSY", "最多同时处理 2 个任务，当前会话最多保留 32 个结果。");
          }
        }
        std::error_code error;
        auto output_directory = std::filesystem::temp_directory_path(error);
        if (error || output_directory.empty()) output_directory = std::filesystem::current_path(error);
        if (error || output_directory.empty()) {
          (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure,
                                "A temporary output directory could not be resolved.");
          return payload_error("E_NATIVE_FAILURE", "A temporary output directory could not be resolved.");
        }
        auto job = std::make_unique<media_job>();
        job->context = context;
        job->task_id = snapshot.task_id;
        job->source = std::move(source_path);
        GUID output_id{};
        if (FAILED(CoCreateGuid(&output_id))) {
          (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure, "Output allocation failed.");
          (void)tasks_.erase(snapshot.task_id);
          return payload_error("E_NATIVE_FAILURE", "Could not allocate output identity.");
        }
        wchar_t output_suffix[40]{};
        StringFromGUID2(output_id, output_suffix, 40);
        output_directory /= std::wstring(L"soui-media-") + output_suffix;
        if (!std::filesystem::create_directory(output_directory, error) || error) {
          (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure, "Output directory failed.");
          (void)tasks_.erase(snapshot.task_id);
          return payload_error("E_NATIVE_FAILURE", "Could not create isolated output directory.");
        }
        job->output = output_directory / ("result" + media_output_extension(format));
        job->encoder = encoder;
        job->format = format;
        job->codec = codec;
        job->quality = static_cast<unsigned>(quality);
        const auto output_name = job->output.filename().generic_string();
        media_job* raw = job.get();
        {
          std::lock_guard<std::mutex> lock(media_mutex_);
          if (media_jobs_.size() >= 32U) {
            std::error_code ignored;
            std::filesystem::remove_all(output_directory, ignored);
            (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure,
                                  "Media capacity reached.");
            (void)tasks_.erase(snapshot.task_id);
            return payload_error("E_BUSY", "Too many retained media tasks; query or restart the session first.");
          }
          media_jobs_.emplace(snapshot.task_id, std::move(job));
        }
        try {
          raw->worker = std::thread([this, raw] { run_media_job(raw); });
        } catch (...) {
          std::lock_guard<std::mutex> lock(media_mutex_);
          media_jobs_.erase(snapshot.task_id);
          std::error_code ignored;
          std::filesystem::remove(output_directory, ignored);
          (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure,
                                "The native media worker could not be started.");
          (void)tasks_.erase(snapshot.task_id);
          return payload_error("E_NATIVE_FAILURE", "The native media worker could not be started.");
        }
        return json{{"taskId", snapshot.task_id}, {"accepted", true}, {"output", output_name}}.dump();
      });
  (void)registry_.register_method(
      "media.convert.query", "media.convert", [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto task_id = string_param(value, "taskId");
        if (task_id.empty()) return payload_error("E_INVALID_PARAMS", "taskId is required.");
        return media_task_json(context, task_id);
      });
  (void)registry_.register_method(
      "media.convert.cancel", "media.convert", [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto task_id = string_param(value, "taskId");
        if (task_id.empty()) return payload_error("E_INVALID_PARAMS", "taskId is required.");
        media_job* job = nullptr;
        {
          std::lock_guard<std::mutex> lock(media_mutex_);
          const auto found = media_jobs_.find(task_id);
          if (found == media_jobs_.end() || found->second->context.session_id != context.session_id ||
              found->second->context.origin != context.origin || found->second->context.token != context.token) {
            return payload_error("E_TASK_NOT_FOUND", "The media task is not valid for this session.");
          }
          job = found->second.get();
        }
        const auto current = tasks_.query(task_id);
        if (!current.has_value()) return payload_error("E_TASK_NOT_FOUND", "The media task does not exist.");
        if (terminal_task_state(current->state)) return payload_error("E_TASK_TERMINAL", "The media task is already complete.");
        job->cancel_requested.store(true, std::memory_order_relaxed);
        if (!tasks_.request_cancel(task_id)) return payload_error("E_TASK_TERMINAL", "The media task is already complete.");
        emit_media_event(*job, "cancel-pending", 0, "Native media cancellation accepted.");
        return json{{"taskId", task_id}, {"cancelRequested", true}}.dump();
      });
  (void)registry_.register_method(
      "desktop.workload.start", "desktop.workload", [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto kind = string_param(value, "kind");
        const auto complexity = size_param(value, "complexity", 1);
        const auto operation = string_param(value, "operationId");
        if ((kind != "video4k" && kind != "cad" && kind != "zip") || operation.empty()) {
          return payload_error("E_INVALID_PARAMS", "kind must be video4k, cad or zip and operationId is required.");
        }
        join_finished_workload_jobs();
        bool duplicate = false;
        bool conflict = false;
        bool capacity = false;
        const auto snapshot = tasks_.start_scoped(operation, context.session_id + "|" + context.origin + "|workload",
                                                  &duplicate, &conflict, &capacity);
        if (conflict) return payload_error("E_OPERATION_CONFLICT", "operationId is already used for another workload.");
        if (capacity) return payload_error("E_BUSY", "The native task store capacity is exhausted.");
        if (duplicate) return json{{"taskId", snapshot.task_id}, {"accepted", true}}.dump();
        auto job = std::make_unique<workload_job>();
        job->context = context;
        job->task_id = snapshot.task_id;
        job->kind = kind;
        job->complexity = complexity;
        workload_job* raw = job.get();
        {
          std::lock_guard<std::mutex> lock(workload_mutex_);
          if (workload_jobs_.size() >= 16U) {
            (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure,
                                  "Workload capacity reached.");
            (void)tasks_.erase(snapshot.task_id);
            return payload_error("E_BUSY", "Too many retained workload tasks.");
          }
          workload_jobs_.emplace(snapshot.task_id, std::move(job));
        }
        try {
          raw->worker = std::thread([this, raw] { run_workload_job(raw); });
        } catch (...) {
          std::lock_guard<std::mutex> lock(workload_mutex_);
          workload_jobs_.erase(snapshot.task_id);
          (void)tasks_.complete(snapshot.task_id, soui::runtime::result_kind::failure,
                                "The native workload worker could not be started.");
          return payload_error("E_NATIVE_FAILURE", "The native workload worker could not be started.");
        }
        return json{{"taskId", snapshot.task_id}, {"accepted", true}}.dump();
      });
  (void)registry_.register_method(
      "desktop.workload.query", "desktop.workload", [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto task_id = string_param(value, "taskId");
        if (task_id.empty()) return payload_error("E_INVALID_PARAMS", "taskId is required.");
        return workload_task_json(context, task_id);
      });
  (void)registry_.register_method(
      "desktop.workload.cancel", "desktop.workload", [this](std::string_view params) -> std::string {
        const std::string value(params);
        const soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto task_id = string_param(value, "taskId");
        if (task_id.empty()) return payload_error("E_INVALID_PARAMS", "taskId is required.");
        workload_job* job = nullptr;
        {
          std::lock_guard<std::mutex> lock(workload_mutex_);
          const auto found = workload_jobs_.find(task_id);
          if (found == workload_jobs_.end() || found->second->context.session_id != context.session_id ||
              found->second->context.origin != context.origin || found->second->context.token != context.token) {
            return payload_error("E_TASK_NOT_FOUND", "The workload task is not valid for this session.");
          }
          job = found->second.get();
        }
        const auto current = tasks_.query(task_id);
        if (!current.has_value()) return payload_error("E_TASK_NOT_FOUND", "The workload task does not exist.");
        if (terminal_task_state(current->state)) return payload_error("E_TASK_TERMINAL", "The workload task is complete.");
        job->cancel_requested.store(true, std::memory_order_relaxed);
        if (!tasks_.request_cancel(task_id)) return payload_error("E_TASK_TERMINAL", "The workload task is complete.");
        emit_workload_event(*job, "cancel-pending", 0, "Native workload cancellation accepted.");
        return json{{"taskId", task_id}, {"cancelRequested", true}}.dump();
      });
  (void)registry_.register_method(
      "desktop.dialog.pickDirectory", "desktop.dialog.pickDirectory", [this](std::string_view params) -> std::string {
        soui::runtime::desktop_request_context context;
        context.session_id = string_param(std::string(params), "sessionId");
        context.origin = string_param(std::string(params), "origin");
        context.token = string_param(std::string(params), "token");
        context.user_gesture = bool_param(std::string(params), "userGesture");
        const auto selected = picker_.open(owner_window_, context);
        if (selected.kind == soui::runtime::result_kind::user_cancelled) {
          return R"({"cancelled":true,"rootToken":""})";
        }
        if (!selected.ok()) return payload_error(selected.code, selected.message);
        const soui::runtime::scan_request_context scan_context{
            context.session_id, context.origin, context.token};
        const auto root = scanner_.register_root(scan_context, selected.value);
        if (root.empty()) return payload_error("E_ROOT_NOT_FOUND", "The selected folder could not be authorized.");
        return "{\"cancelled\":false,\"rootToken\":\"" + json_escape(root) + "\"}";
  });
  (void)registry_.register_method(
      "demo.scan.start", "demo.scan", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto root = string_param(value, "rootToken");
        const auto filter = string_param(value, "filter");
        const auto operation = string_param(value, "operationId");
        if (root.empty() || operation.empty()) return payload_error("E_INVALID_PARAMS", "rootToken and operationId are required.");
        const auto started = scanner_.start(context, operation, root, filter);
        if (!started.ok()) return payload_error(started.code, started.message);
        return "{\"taskId\":\"" + json_escape(started.task_id) + "\",\"accepted\":true}";
      });
  (void)registry_.register_method(
      "filesystem.scan.start", "filesystem.scan", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto root = string_param(value, "rootToken");
        const auto filter = string_param(value, "filter");
        const auto operation = string_param(value, "operationId");
        if (root.empty() || operation.empty()) {
          return payload_error("E_INVALID_PARAMS", "rootToken and operationId are required.");
        }
        const auto started = scanner_.start(context, operation, root, filter, "filesystem.scan.start");
        if (!started.ok()) return payload_error(started.code, started.message);
        return "{\"taskId\":\"" + json_escape(started.task_id) + "\",\"accepted\":true}";
      });
  (void)registry_.register_contextual_method(
      "task.start", "task.start", [this](const soui::runtime::bridge_request& request) {
        return public_tasks_.start(request);
      });
  (void)registry_.register_contextual_method(
      "task.query", "task.query", [this](const soui::runtime::bridge_request& request) -> std::string {
        const std::string value(request.params_json);
        soui::runtime::scan_request_context context{
            request.session_id, request.origin, request.token};
        const auto task_id = string_param(value, "taskId");
        if (task_id.compare(0, 9, "sdk-task-") == 0) return public_tasks_.query(request);
        if (owns_media_task(context, task_id)) return media_task_json(context, task_id);
        if (owns_media_preview_task(context, task_id)) return media_preview_task_json(context, task_id);
        if (owns_workload_task(context, task_id)) return workload_task_json(context, task_id);
        const auto page = scanner_.read_page(context, task_id, string_param(value, "cursor"), 100);
        if (page.kind == soui::runtime::result_kind::failure && page.code != "E_TIMEOUT") {
          return payload_error(page.code, page.message);
        }
        const auto task = tasks_.query(task_id);
        if (!task.has_value()) return payload_error("E_TASK_NOT_FOUND", "The task does not exist.");
        auto result = scan_page_json(page);
        result["taskId"] = task->task_id;
        result["state"] = state_name(task->state);
        result["progress"] = task->progress;
        return result.dump();
  });
  (void)registry_.register_method(
      "demo.scan.page", "task.query", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto task_id = string_param(value, "taskId");
        if (task_id.empty()) return payload_error("E_INVALID_PARAMS", "taskId is required.");
        const auto page = scanner_.read_page(context, task_id, string_param(value, "cursor"),
                                             size_param(value, "pageSize", 100));
        if (page.kind == soui::runtime::result_kind::failure) {
          return payload_error(page.code, page.message);
        }
        return scan_page_json(page).dump();
      });
  (void)registry_.register_method(
      "filesystem.scan.page", "task.query", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::scan_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token")};
        const auto task_id = string_param(value, "taskId");
        if (task_id.empty()) return payload_error("E_INVALID_PARAMS", "taskId is required.");
        const auto page = scanner_.read_page(context, task_id, string_param(value, "cursor"),
                                             size_param(value, "pageSize", 100));
        if (page.kind == soui::runtime::result_kind::failure) {
          return payload_error(page.code, page.message);
        }
        return scan_page_json(page).dump();
      });
  (void)registry_.register_contextual_method(
      "task.cancel", "task.cancel", [this](const soui::runtime::bridge_request& request) -> std::string {
        const std::string value(request.params_json);
        soui::runtime::scan_request_context context{
            request.session_id, request.origin, request.token};
        const auto task_id = string_param(value, "taskId");
        if (task_id.compare(0, 9, "sdk-task-") == 0) return public_tasks_.cancel(request);
        if (owns_media_preview_task(context, task_id)) {
          media_preview_job* job = nullptr;
          {
            std::lock_guard<std::mutex> lock(media_preview_mutex_);
            const auto found = media_preview_jobs_.find(task_id);
            if (found != media_preview_jobs_.end()) job = found->second.get();
          }
          const auto current = tasks_.query(task_id);
          if (!job || !current.has_value()) return payload_error("E_TASK_NOT_FOUND", "The media preview task is not valid for this session.");
          if (terminal_task_state(current->state)) return payload_error("E_TASK_TERMINAL", "The media preview task is complete.");
          job->cancel_requested.store(true, std::memory_order_relaxed);
          if (!tasks_.request_cancel(task_id)) return payload_error("E_TASK_TERMINAL", "The media preview task is complete.");
          emit_media_preview_event(*job, "cancel-pending", 0, "Native media preview cancellation accepted.");
          return json{{"taskId", task_id}, {"state", "cancel-pending"}, {"cancelRequested", true}}.dump();
        }
        if (owns_workload_task(context, task_id)) {
          workload_job* job = nullptr;
          {
            std::lock_guard<std::mutex> lock(workload_mutex_);
            const auto found = workload_jobs_.find(task_id);
            if (found != workload_jobs_.end()) job = found->second.get();
          }
          const auto current = tasks_.query(task_id);
          if (!job || !current.has_value()) return payload_error("E_TASK_NOT_FOUND", "The workload task is not valid for this session.");
          if (terminal_task_state(current->state)) return payload_error("E_TASK_TERMINAL", "The workload task is complete.");
          job->cancel_requested.store(true, std::memory_order_relaxed);
          if (!tasks_.request_cancel(task_id)) return payload_error("E_TASK_TERMINAL", "The workload task is complete.");
          emit_workload_event(*job, "cancel-pending", 0, "Native workload cancellation accepted.");
          return json{{"taskId", task_id}, {"state", "cancel-pending"}, {"cancelRequested", true}}.dump();
        }
        if (owns_media_task(context, task_id)) {
          media_job* job = nullptr;
          {
            std::lock_guard<std::mutex> lock(media_mutex_);
            const auto found = media_jobs_.find(task_id);
            if (found != media_jobs_.end()) job = found->second.get();
          }
          const auto current = tasks_.query(task_id);
          if (!job || !current.has_value()) return payload_error("E_TASK_NOT_FOUND", "The media task is not valid for this session.");
          if (terminal_task_state(current->state)) return payload_error("E_TASK_TERMINAL", "The media task already completed.");
          job->cancel_requested.store(true, std::memory_order_relaxed);
          if (!tasks_.request_cancel(task_id)) return payload_error("E_TASK_TERMINAL", "The media task already completed.");
          emit_media_event(*job, "cancel-pending", 0, "Native media cancellation accepted.");
          return json{{"taskId", task_id}, {"state", "cancel-pending"}, {"cancelRequested", true}}.dump();
        }
        if (!scanner_.owns_task(context, task_id)) {
          return payload_error("E_TASK_NOT_FOUND", "The task is not valid for this session.");
        }
        const auto before = tasks_.query(task_id);
        if (before.has_value() && terminal_task_state(before->state)) {
          return payload_error("E_TASK_TERMINAL", "The task already has a terminal native result.");
        }
        if (!scanner_.cancel(context, task_id)) {
          const auto after = tasks_.query(task_id);
          if (after.has_value() && terminal_task_state(after->state)) {
            return payload_error("E_TASK_TERMINAL", "The task completed before cancellation was accepted.");
          }
          return payload_error("E_TASK_NOT_FOUND", "The task cannot be cancelled.");
        }
        return "{\"taskId\":\"" + json_escape(task_id) + "\",\"state\":\"cancel-pending\",\"cancelRequested\":true}";
      });
  (void)registry_.register_contextual_method(
      "task.subscribe", "task.subscribe", [this](const soui::runtime::bridge_request& request) -> std::string {
        const std::string value(request.params_json);
        const auto task_id = demo_bridge_adapter::string_param(value, "taskId");
        if (task_id.compare(0, 9, "sdk-task-") == 0) return public_tasks_.subscribe(request);
        const auto event = demo_bridge_adapter::string_param(value, "eventName");
        if (task_id.empty() || event.empty()) return payload_error("E_INVALID_PARAMS", "taskId and eventName are required.");
        if (event != "task.progress") return payload_error("E_INVALID_PARAMS", "Only task.progress can be subscribed.");
        const soui::runtime::scan_request_context context{
            request.session_id, request.origin, request.token};
        if (!scanner_.owns_task(context, task_id) && !owns_media_task(context, task_id) &&
            !owns_media_preview_task(context, task_id) && !owns_workload_task(context, task_id)) {
          return payload_error("E_TASK_NOT_FOUND", "The task is not valid for this session.");
        }
        std::string subscription_id;
        {
          std::lock_guard<std::mutex> lock(subscription_mutex_);
          subscription_id = "subscription-" + std::to_string(next_subscription_++);
          subscriptions_.emplace(subscription_id, subscription_binding{context, task_id, event});
#ifdef SOUI_PROGRESS_DEDUP
          // A new observer must not inherit another observer's suppression state.
          running_payloads_.erase(context.session_id + ":" + task_id);
#endif
        }
        return json{{"subscriptionId", subscription_id}}.dump();
      });
  (void)registry_.register_contextual_method(
      "task.unsubscribe", "task.subscribe", [this](const soui::runtime::bridge_request& request) -> std::string {
        const std::string value(request.params_json);
        const soui::runtime::scan_request_context context{
            request.session_id, request.origin, request.token};
        const auto subscription_id = string_param(value, "subscriptionId");
        if (subscription_id.compare(0, 17, "sdk-subscription-") == 0)
          return public_tasks_.unsubscribe(request);
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        const auto found = subscriptions_.find(subscription_id);
        if (found == subscriptions_.end() || found->second.context.session_id != context.session_id ||
            found->second.context.origin != context.origin || found->second.context.token != context.token) {
          return payload_error("E_SUBSCRIPTION_NOT_FOUND", "The subscription is not valid for this session.");
        }
        subscriptions_.erase(found);
        return R"({"removed":true})";
      });
  (void)registry_.register_method(
      "desktop.window.setTitle", "desktop.window.setTitle", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::desktop_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token"), false};
        const auto response = windows_.set_title(owner_window_, context, string_param(value, "title"));
        if (!response.ok()) return payload_error(response.code, response.message);
        return "{\"applied\":true}";
      });
  (void)registry_.register_method(
      "desktop.window.beginDrag", "desktop.window.beginDrag", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::desktop_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token"),
            bool_param(value, "userGesture")};
        const auto response = windows_.begin_drag(owner_window_, context);
        if (!response.ok()) return payload_error(response.code, response.message);
        return R"({"started":true})";
      });
  (void)registry_.register_method(
      "desktop.window.minimize", "desktop.window.minimize", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::desktop_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token"),
            bool_param(value, "userGesture")};
        const auto response = windows_.minimize(owner_window_, context);
        if (!response.ok()) return payload_error(response.code, response.message);
        return R"({"minimized":true})";
      });
  (void)registry_.register_method(
      "desktop.window.toggleMaximize", "desktop.window.toggleMaximize", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::desktop_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token"),
            bool_param(value, "userGesture")};
        const auto response = windows_.toggle_maximize(owner_window_, context);
        if (!response.ok()) return payload_error(response.code, response.message);
        return response.value.empty() ? R"({"maximized":false})" : response.value;
      });
  (void)registry_.register_method(
      "desktop.window.close", "desktop.window.close", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::desktop_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token"),
            bool_param(value, "userGesture")};
        const auto response = windows_.close(owner_window_, context);
        if (!response.ok()) return payload_error(response.code, response.message);
        return R"({"accepted":true})";
      });
  (void)registry_.register_method(
      "desktop.clipboard.writeText", "desktop.clipboard.writeText", [this](std::string_view params) -> std::string {
        const std::string value(params);
        soui::runtime::desktop_request_context context{
            string_param(value, "sessionId"), string_param(value, "origin"), string_param(value, "token"),
            bool_param(value, "userGesture")};
        const auto response = clipboard_.write_text(owner_window_, context, string_param(value, "text"));
        if (!response.ok()) return payload_error(response.code, response.message);
        return "{\"applied\":true}";
      });
}

std::string demo_bridge_adapter::string_param(const std::string& params, const char* name) {
  const auto value = json::parse(params, nullptr, false);
  if (!value.is_object()) return {};
  const auto found = value.find(name);
  return found != value.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

bool demo_bridge_adapter::bool_param(const std::string& params, const char* name) {
  const auto value = json::parse(params, nullptr, false);
  if (!value.is_object()) return false;
  const auto found = value.find(name);
  return found != value.end() && found->is_boolean() && found->get<bool>();
}

std::size_t demo_bridge_adapter::size_param(const std::string& params, const char* name,
                                            std::size_t fallback) {
  const auto value = json::parse(params, nullptr, false);
  if (!value.is_object()) return fallback;
  const auto found = value.find(name);
  if (found == value.end() || !found->is_number_integer()) return fallback;
  const auto parsed = found->get<std::int64_t>();
  return parsed > 0 ? static_cast<std::size_t>(parsed) : fallback;
}

bool demo_bridge_adapter::allow_request(const std::string& session_id) {
  if (session_id.empty()) return false;
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(subscription_mutex_);
  auto& window = request_rates_[session_id];
  if (window.count == 0 || now - window.started >= std::chrono::seconds(1)) {
    window.started = now;
    window.count = 1;
    return true;
  }
  if (window.count >= 240U) return false;
  ++window.count;
  return true;
}

std::string demo_bridge_adapter::augment_context(const soui::runtime::bridge_request& request) {
  auto params = json::parse(request.params_json.empty() ? "{}" : request.params_json, nullptr, false);
  if (!params.is_object()) return request.params_json;
  params["sessionId"] = request.session_id;
  params["origin"] = request.origin;
  params["token"] = request.token;
  return params.dump();
}

}  // namespace soui::demo

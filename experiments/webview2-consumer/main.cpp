#include "webview2_adapter.h"
#include <soui/json_bridge.h>
#include <soui/version.h>
#include <windows.h>
#include <json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using json = nlohmann::json;
using clock_type = std::chrono::steady_clock;
using soui::experiment::webview2_adapter;
using soui::sdk::host_result;

struct options {
  std::filesystem::path resources, profile, output;
  std::string mode{"reload"};
  unsigned cycles{3};
  unsigned timeout_ms{30000};
};

unsigned number(const std::wstring& input, unsigned maximum) {
  if (input.empty() || input.find_first_not_of(L"0123456789") != std::wstring::npos)
    throw std::runtime_error("Expected a positive decimal number");
  const auto value = std::stoul(input);
  if (!value || value > maximum) throw std::runtime_error("Numeric argument outside supported range");
  return static_cast<unsigned>(value);
}

bool within_directory(const std::filesystem::path& candidate, const std::filesystem::path& root) {
  const auto path = candidate.native();
  auto prefix = root.native();
  if (CompareStringOrdinal(path.c_str(), -1, prefix.c_str(), -1, TRUE) == CSTR_EQUAL) return true;
  if (prefix.back() != L'\\') prefix += L'\\';
  return path.size() >= prefix.size() &&
      CompareStringOrdinal(path.data(), static_cast<int>(prefix.size()), prefix.data(),
          static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

options parse_options(int argc, wchar_t** argv) {
  options result;
  for (int i = 1; i < argc; ++i) {
    const std::wstring name = argv[i];
    if (i + 1 >= argc) throw std::runtime_error("Every option requires a value");
    const std::wstring value = argv[++i];
    if (name == L"--resources") result.resources = std::filesystem::absolute(value);
    else if (name == L"--profile") result.profile = std::filesystem::absolute(value);
    else if (name == L"--output") result.output = std::filesystem::absolute(value);
    else if (name == L"--cycles") result.cycles = number(value, 1000);
    else if (name == L"--timeout-ms") result.timeout_ms = number(value, 120000);
    else if (name == L"--mode" && (value == L"reload" || value == L"recreate"))
      result.mode = value == L"reload" ? "reload" : "recreate";
    else throw std::runtime_error("Unknown option or mode");
  }
  if (result.resources.empty() || result.profile.empty() || result.output.empty())
    throw std::runtime_error("Required: --resources <dir> --profile <dir> --output <jsonl>");
  if (!std::filesystem::is_regular_file(result.resources / "index.html"))
    throw std::runtime_error("Controlled resources must contain index.html");
  const auto canonical_root = std::filesystem::canonical(result.resources);
  result.resources = canonical_root;
  result.profile = std::filesystem::weakly_canonical(result.profile);
  result.output = std::filesystem::weakly_canonical(result.output);
  if (within_directory(result.profile, canonical_root) || within_directory(result.output, canonical_root))
    throw std::runtime_error("WebView2 profile and report must be outside the deployed resource directory");
  std::filesystem::create_directories(result.profile);
  if (!result.output.parent_path().empty()) std::filesystem::create_directories(result.output.parent_path());
  return result;
}

std::string read_preload() {
  wchar_t executable[32768]{};
  const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
  if (!length || length == 32768) throw std::runtime_error("Executable path unavailable");
  std::ifstream file(std::filesystem::path(executable).parent_path() / "native-preload.js", std::ios::binary);
  if (!file) throw std::runtime_error("native-preload.js must be adjacent to the executable");
  std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (content.empty() || content.size() > 64U * 1024U) throw std::runtime_error("Invalid preload script size");
  return content;
}

class application {
 public:
  explicit application(options config)
      : config_(std::move(config)), output_(config_.output, std::ios::binary), preload_(read_preload()) {
    try {
    if (!output_) throw std::runtime_error("Cannot create event report");
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"SouiExperimentalWebView2Consumer";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      throw std::runtime_error("Cannot register experiment window");
    window_ = CreateWindowExW(0, type.lpszClassName, L"SOUI SDK — WebView2 experiment",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700, nullptr, nullptr, type.hInstance, this);
    if (!window_) throw std::runtime_error("Cannot create experiment window");
    ShowWindow(window_, SW_SHOWNORMAL);
    // The supervisor suppresses the console via STARTUPINFO. Windows applies
    // that show state to the first ShowWindow call as well; explicitly reveal
    // this GUI test window on the subsequent call when startup hid it.
    if (!IsWindowVisible(window_)) ShowWindow(window_, SW_SHOWNORMAL);
    if (!IsWindowVisible(window_)) throw std::runtime_error("Experiment window must be visible");
    emit("started", {{"pid", GetCurrentProcessId()}, {"cycles", config_.cycles}, {"windowVisible", true}});
    if (!SetTimer(window_, 1, 10, nullptr)) throw std::runtime_error("Cannot create experiment timer");
    begin_cycle();
    } catch (...) {
      host_.reset();
      preparing_.reset();
      bridge_.reset();
      if (window_ && IsWindow(window_)) {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
        window_ = nullptr;
      }
      throw;
    }
  }

  ~application() {
    host_.reset();
    preparing_.reset();
    bridge_.reset();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
  }

  int run() {
    MSG message{};
    for (;;) {
      const auto result = GetMessageW(&message, nullptr, 0, 0);
      if (result == 0) break;
      if (result < 0) { report_failure("E_MESSAGE_LOOP", "Window message loop failed"); break; }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    return failed_ ? 1 : 0;
  }

 private:
  void emit(const char* event, json fields = json::object()) {
    fields["event"] = event;
    fields["cycle"] = cycle_;
    fields["mode"] = config_.mode;
    fields["sdkVersion"] = SOUI_SDK_VERSION_STRING;
    fields["runtimeVersion"] = runtime_version_;
    fields["elapsedMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - began_).count();
    output_ << fields.dump() << '\n';
    output_.flush();
    if (!output_) throw std::runtime_error("Event report write failed");
  }

  void report_failure(const std::string& code, const std::string& message) {
    if (!failed_) {
      failed_ = true;
      emit("failure", {{"code", code}, {"message", message}});
    }
    closing_ = true;
    closing_started_ = clock_type::now();
  }

  void begin_cycle() {
    deadline_ = clock_type::now() + std::chrono::milliseconds(config_.timeout_ms);
    ready_reported_ = false;
    bridge_ = std::make_unique<soui::sdk::json_bridge>("app://webview2-experiment");
    if (!bridge_->register_method("app.ping", "app.ping", [](std::string_view params) {
      const auto value = json::parse(params);
      if (!value.is_object() || !value.empty()) return std::string{R"({"error":"E_INVALID_PARAMS","message":"Expected empty parameters."})"};
      return std::string{R"({"message":"pong"})"};
    })) throw std::runtime_error("Cannot register ping fixture");
    if (!bridge_->register_method("app.fail", "app.fail", [](std::string_view) {
      return std::string{R"({"error":"E_EXPERIMENT_FAILURE","message":"Expected experimental failure."})"};
    })) throw std::runtime_error("Cannot register error fixture");
    if (!bridge_->register_method("app.delay", "app.delay", [](std::string_view params) {
      const auto value = json::parse(params);
      if (!value.is_object() || value.size() != 1 || !value.contains("milliseconds") ||
          !value["milliseconds"].is_number_integer() || value["milliseconds"].get<double>() < 1 ||
          value["milliseconds"].get<double>() > 1000)
        return std::string{R"({"error":"E_INVALID_PARAMS","message":"Expected bounded milliseconds."})"};
      const auto milliseconds = value["milliseconds"].get<unsigned>();
      std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
      return json{{"elapsedMs", milliseconds}}.dump();
    })) throw std::runtime_error("Cannot register delay fixture");
    const auto greet_contract=json{
      {"protocol","soui.action/1"},{"title","问候"},{"description","由C++生成问候消息。"},
      {"inputSchema",{{"type","object"},{"properties",{{"name",{{"type","string"},{"minLength",1},{"maxLength",32}}}}},
                      {"required",json::array({"name"})},{"additionalProperties",false}}},
      {"outputSchema",{{"type","object"},{"properties",{{"message",{{"type","string"},{"maxLength",200}}}}},
                       {"required",json::array({"message"})},{"additionalProperties",false}}}
    }.dump();
    if(!bridge_->register_action("app.greet","app.greet",greet_contract,[](const soui::runtime::bridge_request& request) {
      const auto value=json::parse(request.params_json);
      return json{{"message","Hello, "+value["name"].get<std::string>()+". This response came from C++."}}.dump();
    }))throw std::runtime_error("Cannot register common action contract");
    if(!bridge_->register_method("app.denied","app.secret",[](std::string_view) { return std::string{R"({"secret":true})"}; }))
      throw std::runtime_error("Cannot register denied fixture");
    if(!bridge_->set_session_capabilities({"bridge.describe","bridge.probe","app.ping","app.fail","app.delay","app.greet"}))
      throw std::runtime_error("Cannot configure trusted document capabilities");
    preparing_ = std::make_unique<webview2_adapter>(window_, config_.profile, preload_);
    observed_ = preparing_.get();
    preparing_->prepare_async([this](host_result result) {
      if (!result.ok) { report_failure(result.code, result.message); return; }
      if (closing_) return;
      runtime_version_ = observed_->runtime_version();
      host_ = std::make_unique<soui::sdk::webview_host>(std::move(preparing_));
      soui::sdk::host_config host_config;
      host_config.architecture = sizeof(void*) == 8 ? "x64" : "x86";
      host_config.resource_root = config_.resources;
      host_config.owner_window = window_;
      host_config.bridge_adapter = bridge_.get();
      const auto mounted = host_->start(host_config);
      if (!mounted.ok) report_failure(mounted.code, mounted.message);
    });
  }

  void finish_close() {
    host_result result{true, {}, {}};
    if (host_) result = host_->destroy();
    else if (preparing_) result = preparing_->unmount();
    if (!result.ok) {
      if (result.code == "E_BUSY" && clock_type::now() - closing_started_ < std::chrono::seconds(5)) return;
      report_failure(result.code, result.message);
      // Teardown fallback only joins this fixture's at-most-1000ms native call.
    }
    host_.reset();
    preparing_.reset();
    observed_ = nullptr;
    bridge_.reset();
    if (result.ok) emit("closed");
    if (!failed_ && cycle_ < config_.cycles) {
      ++cycle_;
      closing_ = false;
      begin_cycle();
      return;
    }
    if (!failed_) emit("completed", {{"cycles", config_.cycles}});
    KillTimer(window_, 1);
    DestroyWindow(window_);
    window_ = nullptr;
  }

  void tick() {
    if (closing_) { finish_close(); return; }
    if (clock_type::now() >= deadline_) {
      report_failure("E_TIMEOUT", "Experimental page did not complete before the cycle deadline; " +
          (observed_ ? observed_->application_diagnostics() : std::string("adapter not prepared")));
      return;
    }
    if (observed_) {
      const auto status = observed_->status();
      if (!status.ok && status.code != "E_PENDING") { report_failure(status.code, status.message); return; }
    }
    if (!host_) return;
    host_->pump();
    if (ready_reported_ || !host_->document_ready() || !host_->application_ready()) return;
    const auto fingerprint = observed_->session_digest();
    if (fingerprint.empty()) { report_failure("E_SESSION", "Ready document has no native session identity"); return; }
    ready_reported_ = true;
    emit("ready", {{"sessionDigest", fingerprint}});
    if (config_.mode == "reload" && cycle_ < config_.cycles) {
      ++cycle_;
      ready_reported_ = false;
      deadline_ = clock_type::now() + std::chrono::milliseconds(config_.timeout_ms);
      const auto reloaded = host_->reload();
      if (!reloaded.ok) report_failure(reloaded.code, reloaded.message);
    } else {
      closing_ = true;
      closing_started_ = clock_type::now();
    }
  }

  static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
      try {
        if (message == WM_TIMER && wparam == 1) { self->tick(); return 0; }
        if (message == WM_SIZE && self->host_ && !self->closing_) {
          (void)self->host_->resize(LOWORD(lparam), HIWORD(lparam));
          return 0;
        }
        if (message == WM_SETFOCUS && self->host_ && !self->closing_) { (void)self->host_->focus(); return 0; }
        if (message == WM_CLOSE) { self->report_failure("E_CANCELLED", "Experiment window closed before completion"); return 0; }
        if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
      } catch (const std::exception& exception) {
        try { self->report_failure("E_EXPERIMENT", exception.what()); }
        catch (...) { self->failed_ = true; PostQuitMessage(1); }
        return 0;
      }
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }

  options config_;
  std::ofstream output_;
  std::string preload_, runtime_version_;
  HWND window_{};
  const clock_type::time_point began_{clock_type::now()};
  clock_type::time_point deadline_{}, closing_started_{};
  unsigned cycle_{1};
  bool closing_{false}, failed_{false}, ready_reported_{false};
  std::unique_ptr<soui::sdk::json_bridge> bridge_;
  std::unique_ptr<webview2_adapter> preparing_;
  webview2_adapter* observed_{};
  std::unique_ptr<soui::sdk::webview_host> host_;
};
}  // namespace

int wmain(int argc, wchar_t** argv) {
  const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(apartment)) { std::cerr << "UI STA initialization failed\n"; return 1; }
  int result = 1;
  try {
    application app(parse_options(argc, argv));
    result = app.run();
  } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; }
  CoUninitialize();
  return result;
}

#include <soui/runtime/bridge/bridge_transport.h>
#include "project_file_save.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}
int main() {
  using json = nlohmann::json;
  scratch_project_export storage;
  soui::runtime::bridge_request request;
  request.session_id = "session-a";
  std::atomic_bool closing{false};
  auto call = [&](json packet) {
    request.params_json = packet.dump();
    return json::parse(storage.accept(request, nullptr, closing));
  };
  auto first = call({{"base64", "UEsDBA=="}, {"offset", 0}, {"final", false}});
  require(first["pending"] == true && first["bytes"] == 4, "valid first chunk");
  auto order = call({{"base64", "AAAA"}, {"offset", 5}, {"final", false}});
  require(order["error"] == "E_PROJECT_CHUNK_ORDER", "out of order rejected");
  require(call({{"base64", "UEsDBB=="}, {"offset", 0}, {"final", false}})["error"] ==
      "E_INVALID_PROJECT_BASE64", "noncanonical trailing bits rejected");
  require(call({{"base64", "A==="}, {"offset", 0}, {"final", false}})["error"] ==
      "E_INVALID_PROJECT_BASE64", "invalid padding rejected");
  require(call({{"base64", "AAAA"}, {"offset", -1}, {"final", false}})["error"] ==
      "E_INVALID_PROJECT_CHUNK", "negative offset rejected");
  require(call({{"base64", "AAAA"}, {"offset", 0}, {"final", 1}})["error"] ==
      "E_INVALID_PROJECT_CHUNK", "nonboolean final rejected");
  require(call({{"base64", "AAAA"}, {"offset", 0}, {"final", true}})["error"] ==
      "E_INVALID_PROJECT_ZIP", "nonzip final rejected before any dialog");
  require(call({{"base64", "UEsDBA=="}, {"offset", 0}, {"final", false}, {"path", "unsafe"}})["error"] ==
      "E_INVALID_PROJECT_CHUNK", "page path fields rejected");
  call({{"base64", "UEsDBA=="}, {"offset", 0}, {"final", false}});
  request.session_id = "session-b";
  require(call({{"base64", "AAAA"}, {"offset", 4}, {"final", false}})["error"] ==
      "E_PROJECT_CHUNK_ORDER", "session crossing rejected");
  const std::string block(24576, 'A');
  std::size_t offset = 0;
  for (;;) {
    auto result = call({{"base64", block}, {"offset", offset}, {"final", false}});
    if (result.contains("error")) {
      require(result["error"] == "E_PROJECT_TOO_LARGE", "bounded cumulative quota"); break;
    }
    offset += 18432;
  }
  closing = true;
  require(call({{"base64", "AAAA"}, {"offset", 0}, {"final", false}})["error"] ==
      "E_APPLICATION_CLOSING", "closing application refuses export");

  const auto metrics = [](const json& packet) {
    return scratch_file_save_detail::read_metrics(packet.dump());
  };
  const std::string report = json{{"schema", "soui.scratch-performance/v1"},
      {"scene", "sprites"}, {"results", {{"fps", 59.5}, {"p95", 17.2}}}}.dump();
  const auto accepted = metrics({{"report", report}});
  require(accepted.error.empty() && accepted.text == report, "valid metrics bytes retained without a dialog");
  require(metrics({{"report", report}, {"path", "unsafe.json"}}).error ==
      "E_INVALID_METRICS_REQUEST", "metrics page paths refused");
  require(metrics({{"report", 123}}).error == "E_INVALID_METRICS_REQUEST", "metrics report must be a JSON string");
  require(metrics({{"report", "[1,2]"}}).error == "E_INVALID_METRICS_JSON", "metrics root must be an object");
  require(metrics({{"report", "{bad}"}}).error == "E_INVALID_METRICS_JSON", "malformed metrics JSON refused");
  require(metrics({{"report", "{}"}}).error == "E_INVALID_METRICS_SCHEMA", "metrics schema required");
  require(metrics({{"report", "{\"schema\":false}"}}).error == "E_INVALID_METRICS_SCHEMA", "metrics schema must be text");
  require(metrics({{"report", "{\"schema\":\"other\"}"}}).error == "E_INVALID_METRICS_SCHEMA", "unknown metrics schema refused");
  std::string nested = "0";
  for (unsigned index = 0; index != 18; ++index) nested = "[" + nested + "]";
  require(metrics({{"report", "{\"schema\":\"soui.scratch-performance/v1\",\"nested\":" + nested + "}"}}).error ==
      "E_INVALID_METRICS_JSON", "nested metrics report bounded before parsing");
  const std::string prefix = "{\"schema\":\"soui.scratch-performance/v1\",\"padding\":\"";
  const std::string boundary = prefix + std::string(24U * 1024U - prefix.size() - 2, 'x') + "\"}";
  require(metrics({{"report", boundary}}).error.empty(), "metrics 24 KiB boundary accepted");
  require(metrics({{"report", boundary + " "}}).error == "E_METRICS_TOO_LARGE", "metrics report byte quota enforced");
  const std::string escaped = json{{"schema", "soui.scratch-performance/v1"}, {"padding", std::string(9000, '\\')}}.dump();
  require(escaped.size() < 24U * 1024U && metrics({{"report", escaped}}).error == "E_METRICS_TOO_LARGE",
      "escaped envelope must also fit transport params budget");
  request.params_json = json{{"report", report}}.dump();
  require(json::parse(storage.save_metrics(request, nullptr, closing))["error"] == "E_APPLICATION_CLOSING",
      "closing application refuses metrics before any dialog");
  std::cout << "Scratch export contract checks passed\n";
}

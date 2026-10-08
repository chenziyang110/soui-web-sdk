#pragma once
#include "session_observation.h"
#include <utility>

namespace renewal_probe {
// Pure parser/state transitions. Called by --self-check before COM, Runtime,
// bridge/Host construction or HWND creation. Not executed during v2 preparation.
inline unsigned session_observation_self_check() {
  unsigned cases = 0;
  const auto check = [&](bool condition) { require(condition, "Session observation self-check failed"); ++cases; };
  const auto reject = [&](auto operation) {
    bool rejected = false; try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid session observation was accepted"); ++cases;
  };
  const auto response = [] { return metadata_reader(R"({"requestId":"r","ok":true,"result":{"sessionId":"opaque-authorized","origin":"app://installed-renewal-probe","token":"not-a-real-credential","lease":{"generation":1}}})").read(); };
  const auto bound = [&] { session_phase state; state.begin_bootstrap(); state.bind(response(), "r"); return state; };
  {
    auto state = bound(); state.describe("opaque-authorized", "app://installed-renewal-probe"); state.ready();
    state.begin_close(); check(std::string(state.shutdown("opaque-authorized")) == "bound-close" && state.bound_closes() == 1 && state.prebootstrap_retirements() == 0);
  }
  {
    session_phase state;
    check(std::string(state.shutdown("opaque-host-lifecycle")) == "pre-bootstrap-lifecycle");
    state.begin_bootstrap(); state.bind(response(), "r"); state.describe("opaque-authorized", "app://installed-renewal-probe"); state.ready(); state.begin_close();
    check(std::string(state.shutdown("opaque-authorized")) == "bound-close" && state.shutdown_callbacks() == 2 && state.prebootstrap_retirements() == 1);
  }
  reject([&] { session_phase state; state.shutdown("opaque-first"); state.shutdown("opaque-second"); });
  reject([&] { session_phase state; state.shutdown(""); });
  reject([&] { session_phase state; state.shutdown(std::string(257, 'x')); });
  reject([&] { session_phase state; state.begin_bootstrap(); state.shutdown("opaque-host-lifecycle"); });
  reject([&] { auto state = bound(); state.shutdown("opaque-authorized"); }); // before describe
  reject([&] { auto state = bound(); state.describe("opaque-authorized", "app://installed-renewal-probe"); state.shutdown("opaque-authorized"); }); // before ready
  reject([&] { auto state = bound(); state.describe("opaque-authorized", "app://installed-renewal-probe"); state.ready(); state.shutdown("opaque-authorized"); });
  reject([&] { auto state = bound(); state.begin_close(); state.shutdown("wrong-session"); });
  reject([&] { auto state = bound(); state.begin_close(); state.shutdown("opaque-authorized"); state.shutdown("opaque-authorized"); });
  reject([&] { session_phase state; state.begin_close(); state.shutdown("opaque-host-lifecycle"); });
  reject([&] { session_phase state; state.begin_bootstrap(); state.begin_bootstrap(); });
  reject([&] { auto state = bound(); state.begin_bootstrap(); });
  reject([&] { auto state = bound(); state.bind(response(), "r"); });
  reject([&] { session_phase state; state.shutdown("opaque-authorized"); state.begin_bootstrap(); state.bind(response(), "r"); });
  reject([&] { session_phase state; state.describe("opaque-authorized", "app://installed-renewal-probe"); });
  reject([&] { auto state = bound(); state.describe("changed", "app://installed-renewal-probe"); });
  reject([&] { auto state = bound(); state.ready(); });
  reject([&] { auto state = bound(); state.describe("opaque-authorized", "app://installed-renewal-probe"); state.describe("opaque-authorized", "app://installed-renewal-probe"); });
  reject([&] { session_phase state; state.begin_bootstrap(); state.bind(response(), "wrong-request"); });
  reject([&] { session_phase state; state.begin_bootstrap(); auto value = response(); value.ok = false; state.bind(value, "r"); });
  reject([&] { session_phase state; state.begin_bootstrap(); auto value = response(); value.session_id.clear(); state.bind(value, "r"); });
  reject([&] { session_phase state; state.begin_bootstrap(); auto value = response(); value.origin = "app://wrong"; state.bind(value, "r"); });
  check(metadata_reader(R"({"m\u0065thod":"bridge.\u0062ootstrap","requestId":"r","params":{}})").read().method == "bridge.bootstrap");
  check(metadata_reader(R"({"method":"probe.record","params":{"method":"bridge.bootstrap","ok":true,"result":{"sessionId":"fake"}}})").read().session_id.empty());
  check(metadata_reader(R"({"requestId":"r","ok":true,"result":{"sessionId":"opaque-\uD83D\uDE00","origin":"app://installed-renewal-probe","token":"skipped"}})").read().session_id.size() == 11);
  for (const auto* invalid : {
      R"({"method":"probe.record","m\u0065thod":"bridge.bootstrap"})",
      R"({"method":"bridge.bootstrap","params":{"x":1,"x":2}})",
      R"({"requestId":"r","ok":true,"result":{"sessionId":"x","sessionId":"y"}})",
      R"({"requestId":"r","ok":"true","result":{}})", R"({"requestId":"r","ok":true,"result":[]})",
      R"({"method":false})", R"({"method":"\uD800"})", R"({"method":"\uDC00"})",
      R"({"method":"x","params":01})", R"({"method":"x","params":1e})", R"({"method":"x",})",
      R"({"method":"x"} {})", R"([{"method":"bridge.bootstrap"}])" })
    reject([&] { (void)metadata_reader(invalid).read(); });
  reject([&] { (void)metadata_reader(std::string(65537, ' ')).read(); });
  reject([&] { (void)metadata_reader(std::string("{\"method\":\"") + char(0xc0) + char(0x80) + "\"}").read(); });
  reject([&] { (void)metadata_reader("{\"params\":" + std::string(34, '[') + "0" + std::string(34, ']') + "}").read(); });
  // The return object has no token member; only selected public metadata exists.
  check(response().session_id == "opaque-authorized" && response().origin == "app://installed-renewal-probe");
  return cases;
}
} // namespace renewal_probe

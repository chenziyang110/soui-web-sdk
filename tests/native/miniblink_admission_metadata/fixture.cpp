#include "miniblink_admission_metadata.h"
#include <json.hpp>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

static bool reject_allocations = false;
void* operator new(std::size_t bytes) {
  if (reject_allocations) throw std::bad_alloc();
  if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace original {
#include "original-admission-helpers.inc"
} // namespace original

using metadata = soui::sdk::detail::miniblink_admission_metadata;
using route = soui::sdk::detail::miniblink_request_route;
using json = nlohmann::json;
static_assert(!std::is_copy_constructible_v<metadata> && !std::is_move_constructible_v<metadata>);
static_assert(!std::is_copy_assignable_v<metadata> && !std::is_move_assignable_v<metadata>);
static_assert(std::is_nothrow_constructible_v<metadata,
    std::string&, const std::string&, const std::string&, std::uint32_t>);
static_assert(!std::is_constructible_v<metadata,
    std::string&&, const std::string&, const std::string&, std::uint32_t>);
static_assert(!std::is_constructible_v<metadata,
    std::string&, std::string&&, const std::string&, std::uint32_t>);
static_assert(!std::is_constructible_v<metadata,
    std::string&, const std::string&, std::string&&, std::uint32_t>);

void require(bool condition, const char* name, const char* message) {
  if (!condition) throw std::runtime_error(std::string(name) + ": " + message);
}
struct example final { const char* name; std::string raw; bool accepted; std::uint32_t timeout{10000}; };
std::string packet(std::string method = "benchmark.echo") {
  return json{{"requestId", "r"}, {"method", std::move(method)}, {"params", json::object()}}.dump();
}
std::string sized(std::size_t size) {
  const std::string prefix = "{\"requestId\":\"r\",\"method\":\"benchmark.echo\",\"params\":{\"payload\":\"";
  const std::string suffix = "\"}}";
  return prefix + std::string(size - prefix.size() - suffix.size(), 'x') + suffix;
}
std::string nested(unsigned count) {
  return "{\"requestId\":\"r\",\"method\":\"benchmark.echo\",\"params\":"
      + std::string(count, '[') + "0" + std::string(count, ']') + "}";
}

void move_lifetime(std::string raw, bool fail_after_move) {
  const std::string original_raw = raw;
  const std::string id = "independent-id", method = "bridge.probe.report";
  metadata view(raw, id, method, 60000);
  bool received = false, caught = false;
  const auto receive = [&](std::string owned) {
    require(owned == original_raw, "move", "the bound raw must be transferred intact");
    received = true;
    require(view.request_id() == id && view.method() == method,
        "move", "field views cannot borrow moved raw storage");
    if (fail_after_move) throw 17;
  };
  try { receive(std::move(view.request())); }
  catch (int value) { require(value == 17, "move", "wrong failure"); caught = true; }
  require(received && caught == fail_after_move, "move", "ownership failure path was not visited");
  require(view.request_id() == id && view.method() == method && view.route() == route::probe,
      "move", "caller catch/diagnostic fields must remain alive until synchronous return");
}

int main() {
  try {
    const std::vector<example> cases{
      {"ordinary", packet(), true}, {"probe", packet("bridge.probe.report"), true},
      {"renewal", packet("bridge.session.renew"), true}, {"control", packet("task.cancel"), true},
      {"bootstrap-metadata-is-not-admission", packet("bridge.bootstrap"), true},
      {"escaped-key-value", R"({"requestId":"r","meth\u006fd":"bridge.\u0070robe.report"})", true},
      {"duplicate-last", R"({"requestId":"old","requestId":"r","method":"old","method":"benchmark.echo"})", true},
      {"duplicate-wrong-type", R"({"requestId":"r","method":"benchmark.echo","method":false})", false},
      {"wrong-id-before-timeout", R"({"requestId":5,"method":"benchmark.echo","timeoutMs":0})", false},
      {"wrong-method-before-timeout", R"({"requestId":"r","method":[],"timeoutMs":0})", false},
      {"id-512", json{{"requestId", std::string(512,'i')},{"method","benchmark.echo"}}.dump(), true},
      {"id-513", json{{"requestId", std::string(513,'i')},{"method","benchmark.echo"}}.dump(), false},
      {"method-512", packet(std::string(512,'m')), true},
      {"method-513", packet(std::string(513,'m')), false},
      {"nul-method", packet(std::string("bridge.probe.report") + std::string(1, '\0') + "suffix"), true},
      {"timeout-1", R"({"requestId":"r","method":"benchmark.echo","timeoutMs":1})", true, 1},
      {"timeout-60000", R"({"requestId":"r","method":"benchmark.echo","timeoutMs":60000})", true, 60000},
      {"timeout-0", R"({"requestId":"r","method":"benchmark.echo","timeoutMs":0})", false},
      {"timeout-negative", R"({"requestId":"r","method":"benchmark.echo","timeoutMs":-1})", false},
      {"timeout-float", R"({"requestId":"r","method":"benchmark.echo","timeoutMs":1.0})", false},
      {"timeout-null", R"({"requestId":"r","method":"benchmark.echo","timeoutMs":null})", false},
      {"timeout-string", R"({"requestId":"r","method":"benchmark.echo","timeoutMs":"1"})", false},
      {"timeout-over", R"({"requestId":"r","method":"benchmark.echo","timeoutMs":60001})", false},
      {"timeout-last", R"({"requestId":"r","method":"benchmark.echo","timeoutMs":0,"timeoutMs":17})", true, 17},
      {"nested-only", R"({"params":{"requestId":"r","method":"benchmark.echo"}})", false},
      {"array", R"([{"requestId":"r","method":"benchmark.echo"}])", false},
      {"empty", "", false}, {"invalid", R"({"requestId":"r","method":)", false},
      {"depth-32", nested(31), true}, {"depth-33", nested(32), false},
      {"4k", sized(4096), true}, {"64k", sized(65536), true}, {"over-64k", sized(65537), false}
    };
    for (const auto& test : cases) {
      std::string raw = test.raw;
      const auto first = original::bounded_bridge_packet(raw);
      const std::string id = original::packet_string(first, "requestId");
      const std::string method = original::packet_string(first, "method");
      const auto timeout = id.empty() || method.empty() ? 0U : original::bridge_wait_timeout(first);
      const bool accepted = !id.empty() && !method.empty() && timeout != 0;
      require(accepted == test.accepted, test.name, "original admission-field boundary changed");
      if (!accepted) continue;
      const auto repeated = original::bounded_bridge_packet(raw);
      reject_allocations = true;
      metadata view(raw, id, method, timeout);
      const bool view_ok = &view.request() == &raw && view.request_id().data() == id.data()
          && view.method().data() == method.data() && view.timeout_ms() == test.timeout
          && (view.route() == route::bootstrap) == (method == "bridge.bootstrap")
          && (view.route() == route::probe) == (method == "bridge.probe.report");
      reject_allocations = false;
      require(view_ok, test.name, "borrowed projection/classification must not allocate");
      require(view.request_id() == original::packet_string(repeated, "requestId")
          && view.method() == original::packet_string(repeated, "method")
          && view.timeout_ms() == original::bridge_wait_timeout(repeated),
          test.name, "same raw projection differs from original repeated parse");
    }
    move_lifetime("short", false); move_lifetime("short", true);
    move_lifetime(std::string(65536, 'x'), false); move_lifetime(std::string(65536, 'x'), true);
    std::cout << cases.size() << " parsed-input cases and 4 raw ownership cases passed; "
        << "no Host lifecycle or performance claim\n";
    return 0;
  } catch (const std::exception& failure) {
    reject_allocations = false;
    std::cerr << failure.what() << '\n';
    return 1;
  }
}

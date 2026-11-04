#include "miniblink_request_route.h"
#include <json.hpp>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// Armed only around the classifier, after the JSON and strings already exist.
static bool reject_allocations = false;
void* operator new(std::size_t bytes) {
  if (reject_allocations) throw std::bad_alloc();
  if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace original {
#include "original-methods.inc"
} // namespace original

using route = soui::sdk::detail::miniblink_request_route;
using soui::sdk::detail::classify_miniblink_request_route;
using json = nlohmann::json;
static_assert(sizeof(route) == 1 && std::is_same_v<std::underlying_type_t<route>, std::uint8_t>);
static_assert(noexcept(classify_miniblink_request_route({})));
static_assert(classify_miniblink_request_route({}) == route::ordinary);
static_assert(classify_miniblink_request_route("bridge.bootstrap") == route::bootstrap);
static_assert(classify_miniblink_request_route("bridge.probe.report") == route::probe);

void require(bool condition, const char* test, const char* message) {
  if (!condition) throw std::runtime_error(std::string(test) + ": " + message);
}
struct example final {
  const char* name;
  std::string request;
  std::string expected_method;
  route expected_route{route::ordinary};
};
std::string with_method(std::string method) {
  return json{{"requestId", "r"}, {"method", std::move(method)}}.dump();
}
std::string nested(unsigned levels) {
  return std::string{"{\"method\":\"bridge.bootstrap\",\"params\":"}
      + std::string(levels, '[') + "0" + std::string(levels, ']') + "}";
}
std::string sized_request(std::size_t bytes) {
  const std::string prefix = "{\"requestId\":\"r\",\"method\":\"bridge.bootstrap\",\"params\":{\"payload\":\"";
  const std::string suffix = "\"}}";
  return prefix + std::string(bytes - prefix.size() - suffix.size(), 'x') + suffix;
}

int main() {
  try {
    const std::string boot = "bridge.bootstrap", probe = "bridge.probe.report";
    const std::string nul_boot = boot + std::string(1, '\0') + "suffix";
    const std::string nul_probe = probe + std::string(1, '\0');
    const std::vector<example> examples{
      {"bootstrap", with_method(boot), boot, route::bootstrap},
      {"probe", with_method(probe), probe, route::probe},
      {"ordinary", with_method("benchmark.echo"), "benchmark.echo"},
      {"unknown-name", with_method("bridge.unknown"), "bridge.unknown"},
      {"escaped-method-value", R"({"method":"bridge.\u0062ootstrap"})", boot, route::bootstrap},
      {"escaped-method-key", R"({"meth\u006fd":"bridge.probe.report"})", probe, route::probe},
      {"duplicate-last-bootstrap", R"({"method":"ordinary","method":"bridge.bootstrap"})", boot, route::bootstrap},
      {"duplicate-last-ordinary", R"({"method":"bridge.bootstrap","method":"ordinary"})", "ordinary"},
      {"duplicate-last-probe", R"({"method":"bridge.bootstrap","method":"bridge.probe.report"})", probe, route::probe},
      {"duplicate-last-wrong-type", R"({"method":"bridge.bootstrap","method":false})", ""},
      {"duplicate-escaped-key", R"({"method":"bridge.bootstrap","meth\u006fd":"ordinary"})", "ordinary"},
      {"nested-method-only", R"({"params":{"method":"bridge.bootstrap"}})", ""},
      {"nested-method-not-top", R"({"method":"ordinary","params":{"method":"bridge.probe.report"}})", "ordinary"},
      {"payload-text", R"({"method":"ordinary","params":{"text":"bridge.bootstrap"}})", "ordinary"},
      {"prefix", with_method("x" + boot), "x" + boot},
      {"suffix", with_method(boot + "x"), boot + "x"},
      {"case", with_method("Bridge.bootstrap"), "Bridge.bootstrap"},
      {"whitespace-in-method", with_method(boot + " "), boot + " "},
      {"embedded-nul-bootstrap", with_method(nul_boot), nul_boot},
      {"embedded-nul-probe", with_method(nul_probe), nul_probe},
      {"empty-method", with_method(""), ""},
      {"missing-method", R"({"requestId":"r"})", ""},
      {"null-method", R"({"method":null})", ""},
      {"numeric-method", R"({"method":17})", ""},
      {"boolean-method", R"({"method":true})", ""},
      {"array-method", R"({"method":["bridge.bootstrap"]})", ""},
      {"object-method", R"({"method":{"value":"bridge.bootstrap"}})", ""},
      {"method-512", with_method(std::string(512, 'm')), std::string(512, 'm')},
      {"method-513", with_method(std::string(513, 'm')), ""},
      {"top-array", R"([{"method":"bridge.bootstrap"}])", ""},
      {"top-string", R"("bridge.bootstrap")", ""},
      {"top-null", "null", ""},
      {"empty-packet", "", ""},
      {"truncated-packet", R"({"method":"bridge.bootstrap")", ""},
      {"trailing-junk", R"({"method":"bridge.bootstrap"}x)", ""},
      {"invalid-escape", R"({"method":"bridge.\qootstrap"})", ""},
      {"invalid-utf8", std::string{"{\"method\":\"bridge.bootstrap\",\"params\":\""} + static_cast<char>(0xff) + "\"}", ""},
      {"depth-32", nested(31), boot, route::bootstrap},
      {"depth-33", nested(32), ""},
      {"packet-65536", sized_request(65536), boot, route::bootstrap},
      {"packet-65537", sized_request(65537), ""}
    };
    for (const auto& test : examples) {
      const auto packet = original::bounded_bridge_packet(test.request);
      const auto admitted_method = original::packet_string(packet, "method");
      const auto direct_method = original::bridge_request_method(test.request);
      require(admitted_method == test.expected_method && direct_method == test.expected_method,
          test.name, "original bounded extraction changed");
      reject_allocations = true;
      const auto cached = classify_miniblink_request_route(admitted_method);
      reject_allocations = false;
      require(cached == test.expected_route && cached != route::unknown,
          test.name, "parsed method must have an exact known route");
      // These are the original Host's two string comparisons, not a copied Host.
      require((cached == route::bootstrap) == (direct_method == "bridge.bootstrap")
          && (cached == route::probe) == (direct_method == "bridge.probe.report"),
          test.name, "queued classification changes direct response bookkeeping");
    }
    // View bounds matter: never use strlen or retain a borrowed method reference.
    std::string backing = boot + "ignored";
    const std::string_view bounded(backing.data(), boot.size());
    reject_allocations = true;
    const auto captured = classify_miniblink_request_route(bounded);
    reject_allocations = false;
    backing.assign("changed");
    require(captured == route::bootstrap, "bounded-view", "classification must be a value");
    require(classify_miniblink_request_route(backing) == route::ordinary,
        "modified-source", "classification retained old input state");
    std::cout << examples.size() << " exact-source method cases + bounded view/value lifetime passed; "
        "Host lifecycle/authorization/runtime not executed\n";
    return 0;
  } catch (const std::exception& error) {
    reject_allocations = false;
    std::cerr << error.what() << '\n';
    return 1;
  }
}

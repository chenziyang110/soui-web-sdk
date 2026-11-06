#include "../../../sdk/renderers/webview2/request_dispatch_metadata.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <vector>

// Fault injection is armed only around cache capture, after the input DOM exists.
static int allocations_before_failure = -1;
void* operator new(std::size_t bytes) {
  if (allocations_before_failure == 0) { allocations_before_failure = -1; throw std::bad_alloc(); }
  if (allocations_before_failure > 0) --allocations_before_failure;
  if (void* pointer = std::malloc(bytes ? bytes : 1)) return pointer;
  throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

using json = nlohmann::json;
using metadata = soui::sdk::detail::request_dispatch_metadata;

// Differential oracle: preserve the old dispatch's short-circuit/error contract.
std::string original(const json& packet, const std::string& session, bool expired) {
  try {
    if (expired) return "timeout";
    const auto method = packet.value("method", std::string{});
    if ((method == "bridge.bootstrap" && !session.empty()) ||
        (method != "bridge.bootstrap" && (session.empty() || packet.value("sessionId", std::string{}) != session)))
      return "unauthorized";
    return "dispatch:" + method;
  } catch (...) { return "exception"; }
}
std::string cached_with(const json& packet, const std::string& session, bool expired, const metadata& fields) {
  try {
    if (expired) return "timeout";
    const auto fallback = fields.available() ? json{} : json::parse(packet.dump());
    const auto method = fields.available() ? fields.method() : fallback.value("method", std::string{});
    if ((method == "bridge.bootstrap" && !session.empty()) ||
        (method != "bridge.bootstrap" && (session.empty() ||
            (fields.available() ? fields.session() : fallback.value("sessionId", std::string{})) != session)))
      return "unauthorized";
    return "dispatch:" + method;
  } catch (...) { return "exception"; }
}
std::string cached(const json& packet, const std::string& session, bool expired) {
  return cached_with(packet, session, expired, metadata::capture(packet));
}
int main() {
  const std::vector<json> packets{
    json::object(), {{"method","bridge.bootstrap"}},
    {{"method","bridge.bootstrap"},{"sessionId",7}},
    {{"method","demo.echo"},{"sessionId",7}},
    {{"method",7},{"sessionId","owned"}},
    {{"method",nullptr},{"sessionId",false}},
    {{"method","demo.echo"}}, {{"method","demo.echo"},{"sessionId","owned"}},
    {{"method","demo.echo"},{"sessionId","other"}},
    {{"method",std::string(128,'a')},{"sessionId",std::string(256,'b')}},
    {{"method",std::string(129,'a')},{"sessionId","owned"}},
    {{"method","demo.echo"},{"sessionId",std::string(257,'b')}},
    {{"method",std::string("demo.\0echo",10)},{"sessionId","owned"}},
    json::parse(R"({"method":"demo.\u0065cho","sessionId":"own\u0065d"})"),
    json::parse(R"({"method":"old","method":"bridge.bootstrap","sessionId":[]})")
  };
  unsigned cases=0;
  for(const auto& packet:packets) for(const auto& session:std::vector<std::string>{"","owned"})
    for(bool expired:{false,true}) {
      assert(original(packet,session,expired)==cached(packet,session,expired)); ++cases;
    }
  auto packet=json{{"method","demo.echo"},{"sessionId","owned"}};
  const auto fields=metadata::capture(packet);
  packet["method"]="changed"; packet["sessionId"]="changed";
  assert(fields.available() && fields.method()=="demo.echo" && fields.session()=="owned");
  assert(metadata::capture(packets[9]).available());
  assert(metadata::capture(packets[9]).cached_payload_bytes()==384);
  assert(!metadata::capture(packets[10]).available());
  assert(!metadata::capture(packets[11]).available());
  assert(!metadata::capture(json::array()).available());
  const auto allocates=json{{"method",std::string(64,'m')},{"sessionId",std::string(64,'s')}};
  for(int fail_after:{0,1}) {
    allocations_before_failure=fail_after;
    const auto failed=metadata::capture(allocates);
    allocations_before_failure=-1;
    assert(!failed.available() && failed.cached_payload_bytes()==0);
    assert(original(allocates,std::string(64,'s'),false)==cached_with(allocates,std::string(64,'s'),false,failed));
  }
  std::cout<<cases<<" dispatch differential cases + cache boundary/immutability passed\n";
}

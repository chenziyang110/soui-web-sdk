#include "../../../sdk/renderers/webview2/request_dispatch_metadata.h"
#include <chrono>
#include <iostream>
#include <string>

using json = nlohmann::json;
using metadata = soui::sdk::detail::request_dispatch_metadata;
volatile std::size_t observed_bytes = 0;

// CPU slice only: ingress DOM + ownership fields, excluding Bridge authorization,
// COM, queues, renderer and presentation. Not an input/FPS acceptance benchmark.
void run(const std::string& raw, unsigned count, bool cache, unsigned round) {
  const auto begin = std::chrono::steady_clock::now();
  std::size_t observed=0;
  for(unsigned iteration=0; iteration<count; ++iteration) {
    const auto ingress = json::parse(raw);
    const auto fields = cache ? metadata::capture(ingress) : metadata{};
    const auto reparsed = fields.available() ? json{} : json::parse(raw);
    const auto method = fields.available() ? fields.method() : reparsed.value("method",std::string{});
    const auto session = fields.available() ? fields.session() : reparsed.value("sessionId",std::string{});
    observed += method.size()+session.size();
  }
  observed_bytes = observed;
  const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
  std::cout << json{{"scope","ingress-and-dispatch-fields-cpu-only"},{"cached",cache},
    {"bytes",raw.size()},{"iterations",count},{"round",round},{"elapsed_ns",ns},
    {"observed_bytes",observed}}.dump() << '\n';
}
int main() {
  for(const auto bytes:{64U,4096U,64000U}) {
    const auto raw=json{{"requestId","r"},{"method","demo.echo"},{"sessionId","owned"},
      {"params",{{"text",std::string(bytes,'x')}}}}.dump();
    const unsigned count=bytes==64U?5000U:bytes==4096U?1000U:100U;
    // Alternation controls basic drift. Independent process/noise controls are
    // still required for adoption or user-visible performance claims.
    run(raw,20,false,0); run(raw,20,true,0);
    run(raw,count,false,1); run(raw,count,true,1);
    run(raw,count,true,2); run(raw,count,false,2);
  }
}

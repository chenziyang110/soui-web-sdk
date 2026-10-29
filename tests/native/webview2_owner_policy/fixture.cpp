#include "../../../sdk/renderers/webview2/owner_output_policy.h"
#include "../../../sdk/renderers/webview2/owner_callout_policy.h"
#include <memory>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <limits>
using budget = soui::sdk::detail::owner_output_budget;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
  try {
    const budget::clock::time_point start{};
    budget legacy(0, start), timed(1000, start), invalid(5001, start);
    require(legacy.allows_next(15, start + std::chrono::seconds(1)) && !legacy.allows_next(16, start), "legacy16-item boundary");
    require(timed.allows_next(0, start + std::chrono::seconds(1)), "at least one output must progress");
    require(timed.allows_next(1, start + std::chrono::microseconds(999)) && !timed.allows_next(1, start + std::chrono::microseconds(1000)), "elapsed candidate deadline");
    require(invalid.allows_next(15, start + std::chrono::seconds(1)), "invalid config changed default policy");
    require(budget::may_cache(32, 16, 32 + 16 * sizeof(wchar_t)), "exact prepared byte bound");
    require(!budget::may_cache(33, 16, 32 + 16 * sizeof(wchar_t)), "prepared representation exceeds existing lane byte bound");
    require(!budget::may_cache(65, 0, 64), "oversized raw underflowed capacity");
    require(!budget::may_cache(0, (std::numeric_limits<std::size_t>::max)(), 64), "prepared byte calculation overflowed");
    require(budget::parse_microseconds(L"1000", 4) == 1000 && budget::parse_microseconds(L"100x", 4) == 0
        && budget::parse_microseconds(L"9999999999", 10) == 0, "invalid owner budget was accepted");
    require(budget::parse_microseconds(L"100", 3) == 100 && budget::parse_microseconds(L"5000", 4) == 5000
        && budget::parse_microseconds(L"99", 2) == 0 && budget::parse_microseconds(L"5001", 4) == 0
        && budget::parse_microseconds(L"-100", 4) == 0, "owner budget range/strict decimal boundary");
    struct fake_core { unsigned& retired; explicit fake_core(unsigned& value) : retired(value) {} ~fake_core() { ++retired; } };
    unsigned retired = 0, executed = 0; bool mounted = true, pending = true;
    std::uint64_t generation = 1;
    auto current = std::make_shared<fake_core>(retired);
    const auto target = current; const auto expected_generation = generation;
    const auto identity = [&] { return mounted && generation == expected_generation && current == target; };
    const bool still_current = soui::sdk::detail::owner_callout_current(identity, [&] {
      current.reset(); mounted = false; return true; // get_Source reenters close, but returns an old trusted URL.
    });
    if (still_current) { ++executed; pending = false; }
    require(!still_current && !executed && pending && !retired, "opaque getter close used a reset core or retired the actual target early");
    mounted = true; current = target; generation = 1;
    const bool old_document = soui::sdk::detail::owner_callout_current(identity, [&] { ++generation; return true; });
    if (old_document) pending = false;
    require(!old_document && pending, "old completion changed the new document diagnostic flag");
    bool running = false;
    {
      const soui::sdk::detail::owner_pump_guard outer(running), nested(running);
      require(static_cast<bool>(outer) && !static_cast<bool>(nested) && running, "opaque COM reentry opened a second owner batch");
    }
    require(!running, "owner batch guard did not retire after return");
    std::cout << "WebView2 owner policy source contract complete\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

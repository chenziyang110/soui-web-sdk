#pragma once
#include "../../../sdk/src/host/host_visibility_policy.h"
#include <stdexcept>
namespace host_visibility_cases {
using policy = soui::sdk::detail::host_visibility_policy;
inline void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline void run() {
  std::atomic<bool> intent{false}, dirty{true}; std::atomic<int> applied{-1};
  unsigned calls = 0; bool last = true;
  check(!policy::apply_one(intent, dirty, applied, [&](bool target) { ++calls; last = target; return false; })
      && applied == -1 && policy::pending(intent, dirty, applied), "failed visibility publication faked applied state");
  check(policy::apply_one(intent, dirty, applied, [&](bool target) { ++calls; last = target; return true; })
      && !last && applied == 0 && !policy::pending(intent, dirty, applied), "visibility retry lost latest hidden intent");
  policy::request(intent, dirty, true);
  check(policy::apply_one(intent, dirty, applied, [&](bool target) { ++calls; last = target; return true; })
      && last && applied == 1, "restore publication failed");
  const auto steady_calls = calls;
  check(policy::apply_one(intent, dirty, applied, [&](bool) { ++calls; return true; }) && calls == steady_calls,
      "unchanged visibility generated repeated owner work");
  policy::request(intent, dirty, false); policy::request(intent, dirty, true);
  check(policy::apply_one(intent, dirty, applied, [&](bool target) { ++calls; last = target; return true; })
      && last && calls == steady_calls + 1, "coalesced hide/show lost full-refresh publication");
  policy::request(intent, dirty, false);
  check(!policy::apply_one(intent, dirty, applied, [&](bool target) {
    ++calls; last = target; policy::request(intent, dirty, true); return true;
  }) && !last && applied == 0 && policy::pending(intent, dirty, applied), "in-flight publication consumed newer visibility intent");
  check(policy::apply_one(intent, dirty, applied, [&](bool target) { ++calls; last = target; return true; })
      && last && applied == 1, "newer restore intent failed bounded retry");
  policy::invalidate(dirty, applied);
  check(applied == -1 && policy::pending(intent, dirty, applied), "closed/recreated view inherited an old visibility acknowledgement");
}
} // namespace host_visibility_cases

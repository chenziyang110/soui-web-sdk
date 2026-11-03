#pragma once

// Private policy decision, separated from AppKit only for deterministic tests.
// A setter reports a transition attempt, not the application's observed state.
namespace soui::desktop::detail {
struct activation_policy_result {
  bool regular;
  bool attempted;
  bool setter_accepted;
};

template <typename RequestRegular, typename ObserveRegular>
activation_policy_result ensure_regular_activation_policy(bool initially_regular,
    RequestRegular&& request_regular, ObserveRegular&& observe_regular) {
  if (initially_regular) return {true, false, false};
  const bool accepted = request_regular();
  const bool observed = observe_regular();
  return {observed, true, accepted};
}
}  // namespace soui::desktop::detail

#include "../../sdk/src/desktop/macos_activation_policy.h"
#include <iostream>

int main() {
  int failures = 0;
  unsigned cases = 0;
  for (bool initial : {false, true}) {
    for (bool setter : {false, true}) {
      for (bool observed : {false, true}) {
        ++cases;
        int requests = 0, observations = 0;
        const auto result = soui::desktop::detail::ensure_regular_activation_policy(initial,
            [&] { ++requests; return setter; },
            [&] { ++observations; return observed; });
        const bool valid = initial
            ? result.regular && !result.attempted && !result.setter_accepted && requests == 0 && observations == 0
            : result.regular == observed && result.attempted && result.setter_accepted == setter &&
              requests == 1 && observations == 1;
        if (!valid) ++failures;
      }
    }
  }
  std::cout << cases << " activation policy cases, " << failures << " failures\n";
  return failures ? 1 : 0;
}

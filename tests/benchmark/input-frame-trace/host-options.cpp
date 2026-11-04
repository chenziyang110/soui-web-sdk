#include "../../../benchmarks/input-ui-host/harness_options.h"
#include <iostream>
#include <stdexcept>
void need(bool condition) { if (!condition) throw std::runtime_error("Harness option contract failed"); }
int main() {
  try {
    soui::benchmark::harness_options formal;
    need(!formal.diagnostic() && !formal.suppress_focus && !formal.borderless && !formal.foreground && !formal.logging);
    need(!formal.consume(L"--unknown") && !formal.diagnostic());
    for(const auto* flag : {L"--diagnostic-suppress-focus",L"--diagnostic-borderless",L"--diagnostic-foreground",L"--diagnostic-log"}) {
      soui::benchmark::harness_options diagnostic;
      need(diagnostic.consume(flag) && diagnostic.diagnostic());
      bool duplicate=false;try { (void)diagnostic.consume(flag); } catch(const std::invalid_argument&) { duplicate=true; }
      need(duplicate);
    }
    std::cout << "Harness default and diagnostic boundary cases passed\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#include "supervisor_clock.h"
#include <iostream>
#include <stdexcept>
struct observation { std::uint64_t last_record_at; std::uint64_t finish_at; };
struct state {
  observation value;
  std::uint64_t clock;
  bool publish_during_clock_read;
  observation snapshot() const { return value; }
  std::uint64_t now() {
    const auto captured = clock;
    // A real bridge callback may publish a timestamp after clock capture,
    // before the caller takes its separately locked observation snapshot.
    if (publish_during_clock_read) {
      value.last_record_at = ++clock;
      value.finish_at = clock;
    }
    return captured;
  }
};
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
  try {
    unsigned checks = 0;
    state raced{{1000,1000},1000,true};
    const auto sample = renewal_probe::sample_clock(raced);
    require(sample.now - sample.observed.last_record_at <= 30000, "Fresh callback falsely exceeded record gap after interleaving"); ++checks;
    require(sample.now - sample.observed.finish_at < 500, "Fresh finish falsely satisfied close grace after interleaving"); ++checks;
    for (const auto age : {std::uint64_t{29999},std::uint64_t{30000},std::uint64_t{30001}}) {
      state stale{{1000,1000},1000+age,false};const auto observed=renewal_probe::sample_clock(stale);
      require((observed.now-observed.observed.last_record_at>30000)==(age>30000),"Record gap boundary changed");++checks;
    }
    for (const auto age : {std::uint64_t{499},std::uint64_t{500},std::uint64_t{501}}) {
      state closed{{1000,1000},1000+age,false};const auto observed=renewal_probe::sample_clock(closed);
      require((observed.now-observed.observed.finish_at>=500)==(age>=500),"Close grace boundary changed");++checks;
    }
    std::cout << "Supervisor clock checks passed: " << checks << '\n'; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

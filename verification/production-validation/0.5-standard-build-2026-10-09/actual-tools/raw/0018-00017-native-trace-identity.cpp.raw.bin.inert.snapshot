#include "trace-identity.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace identity = soui::benchmark;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
  try {
    require(identity::parse_expected_filetime(L"1") == 1, "Minimum canonical identity");
    require(identity::parse_expected_filetime(L"18446744073709551615") ==
        std::numeric_limits<std::uint64_t>::max(), "Maximum uint64 identity");
    for (const auto text : {L"", L"0", L"01", L"+1", L"-1", L"1.0", L"1e3", L" 1", L"1 ",
        L"18446744073709551616", L"999999999999999999999999999999999999999999"}) {
      bool rejected = false;
      try { (void)identity::parse_expected_filetime(text); }
      catch (const std::runtime_error&) { rejected = true; }
      require(rejected, "Malformed or overflow identity must fail before acquisition");
    }
    constexpr std::uint64_t creation = 134000000000000001ULL;
    constexpr std::uint64_t started = 134000000100000000ULL;
    for (const auto schema : {identity::identity_schema::legacy_v1, identity::identity_schema::exact_v2}) {
      std::cout << identity::input_begin(812, "S01", "full", creation, started + 10000001ULL,
          10000000, creation, schema);
      std::cout << identity::process_begin(812, creation, started, 1000, 90000, creation, schema);
    }
    std::cout << "{\"event\":\"fixture.boundary\",\"maximum\":" <<
        identity::encode_filetime(std::numeric_limits<std::uint64_t>::max()) <<
        ",\"hostBits\":" << sizeof(void*) * 8 << "}\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

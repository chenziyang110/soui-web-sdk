#include <soui/runtime/services/permissions/permission_store.h>

#include "../../sdk/src/runtime/diagnostics/runtime_metrics_internal.h"
#include "../../sdk/src/runtime/platform/secure_random.h"
#include "../../sdk/src/runtime/platform/strict_utf8.h"
#include "../../sdk/src/runtime/platform/uuid_v4.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace soui::runtime {
struct permission_store_test_access {
  static void set_entropy(permission_store& store, bool (*source)(unsigned char*, std::size_t)) {
    store.entropy_ = source;
  }
  static void restore_entropy(permission_store& store) {
    store.entropy_ = &permission_store::system_entropy;
  }
};
}  // namespace soui::runtime

namespace {
namespace rt = soui::runtime;
namespace dm = soui::sdk::diagnostics;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename Exception, typename Function>
void expect_throw(Function&& function, const char* message) {
  bool caught = false;
  try { function(); }
  catch (const Exception&) { caught = true; }
  require(caught, message);
}

std::string encode_code_point(std::uint32_t point) {
  std::string result;
  if (point < 0x80U) {
    result += static_cast<char>(point);
  } else if (point < 0x800U) {
    result += static_cast<char>(0xc0U | (point >> 6U));
    result += static_cast<char>(0x80U | (point & 0x3fU));
  } else if (point < 0x10000U) {
    result += static_cast<char>(0xe0U | (point >> 12U));
    result += static_cast<char>(0x80U | ((point >> 6U) & 0x3fU));
    result += static_cast<char>(0x80U | (point & 0x3fU));
  } else {
    result += static_cast<char>(0xf0U | (point >> 18U));
    result += static_cast<char>(0x80U | ((point >> 12U) & 0x3fU));
    result += static_cast<char>(0x80U | ((point >> 6U) & 0x3fU));
    result += static_cast<char>(0x80U | (point & 0x3fU));
  }
  return result;
}

void check_utf8(const std::string& bytes, bool expected) {
  require(rt::detail::valid_utf8(bytes) == expected, "UTF-8 scalar validation mismatch");
#if defined(_WIN32)
  if (!bytes.empty()) {
    const bool native = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0) > 0;
    require(native == expected, "UTF-8 validation changed Windows acceptance");
  }
#endif
}

void test_utf8() {
  check_utf8({}, true);
  check_utf8("ASCII", true);
  check_utf8(std::string("a\0b", 3), true);  // Field policy separately rejects NUL.
  // Exhaust the scalar range, including boundaries and noncharacters. The
  // surrogate interval is deliberately encoded to prove it is rejected.
  for (std::uint32_t point = 0; point <= 0x10ffffU; ++point)
    check_utf8(encode_code_point(point), point < 0xd800U || point > 0xdfffU);
  const std::vector<std::string> malformed{
      "\x80", "\xbf", "\xc0\x80", "\xc1\xbf", "\xc2", "\xdf",
      "\xc2\x7f", "\xc2\xc0", "\xe0\x80\x80", "\xe0\x9f\xbf",
      "\xe0\xa0", "\xe1\x80", "\xe1\x80\x7f", "\xed\xa0\x80",
      "\xed\xbf\xbf", "\xf0\x80\x80\x80", "\xf0\x8f\xbf\xbf",
      "\xf0\x90\x80", "\xf1\x80\x80", "\xf1\x80\x80\x7f",
      "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xf8\x88\x80\x80\x80",
      "\xfc\x84\x80\x80\x80\x80", "\xfe", "\xff"};
  for (const auto& bytes : malformed) {
    check_utf8(bytes, false);
    check_utf8("prefix" + bytes + "suffix", false);
  }
}

void test_entropy_reader() {
  std::array<unsigned char, 600> bytes{};
  std::size_t offset = 0;
  std::size_t calls = 0;
  require(rt::detail::fill_entropy_from_reader(bytes.data(), bytes.size(),
      [&](unsigned char* output, std::size_t size) -> std::ptrdiff_t {
        require(output == bytes.data() + offset, "Short read lost output position");
        require(size && size <= 256U, "Entropy syscall request was not bounded");
        ++calls;
        if (calls == 1 || calls == 3) { errno = EINTR; return -1; }
        const auto count = (std::min)(size, std::size_t{17});
        std::fill(output, output + count, static_cast<unsigned char>(0xa5));
        offset += count;
        return static_cast<std::ptrdiff_t>(count);
      }), "Short reads or EINTR prevented complete entropy read");
  require(offset == bytes.size(), "Entropy reader accepted an incomplete result");
  require(std::all_of(bytes.begin(), bytes.end(), [](unsigned char byte) { return byte == 0xa5; }),
      "Entropy reader left bytes unfilled");

  for (int error : {EAGAIN, ENOSYS, EIO}) {
    calls = 0;
    require(!rt::detail::fill_entropy_from_reader(bytes.data(), 32,
        [&](unsigned char*, std::size_t) -> std::ptrdiff_t {
          ++calls; errno = error; return -1;
        }), "Unavailable entropy was accepted");
    require(calls == 1, "Permanent entropy error was retried");
  }
  calls = 0;
  require(!rt::detail::fill_entropy_from_reader(bytes.data(), 32,
      [&](unsigned char*, std::size_t) -> std::ptrdiff_t {
        ++calls; errno = EINTR; return -1;
      }), "Continuous interruptions did not fail closed");
  require(calls == 9, "Entropy interruption retry bound changed");
  require(!rt::detail::fill_entropy_from_reader(bytes.data(), 32,
      [](unsigned char*, std::size_t) -> std::ptrdiff_t { return 0; }),
      "Zero-byte entropy result was accepted");
  calls = 0;
  require(!rt::detail::fill_entropy_from_reader(bytes.data(), 32,
      [&](unsigned char* output, std::size_t) -> std::ptrdiff_t {
        if (!calls++) { *output = 0xa5; return 1; }
        errno = EIO; return -1;
      }), "Partial entropy followed by an error was accepted");
  require(!rt::detail::fill_entropy_from_reader(bytes.data(), 32,
      [](unsigned char*, std::size_t size) -> std::ptrdiff_t {
        return static_cast<std::ptrdiff_t>(size + 1);
      }), "Out-of-range entropy result was accepted");
  calls = 0;
  auto untouched = [&](unsigned char*, std::size_t) -> std::ptrdiff_t { ++calls; return 0; };
  require(rt::detail::fill_entropy_from_reader(nullptr, 0, untouched), "Empty entropy request failed");
  require(!rt::detail::fill_entropy_from_reader(nullptr, 1, untouched), "Null entropy output accepted");
  require(calls == 0, "Invalid or empty entropy request reached the reader");
  require(rt::detail::secure_random(nullptr, 0), "Empty system entropy request failed");
  require(!rt::detail::secure_random(nullptr, 1), "Null system entropy output accepted");
}

void test_uuid() {
  std::array<unsigned char, 16> bytes{};
  require(rt::detail::format_uuid_v4(bytes) == "00000000-0000-4000-8000-000000000000",
      "UUID version/variant bit placement changed");
  bytes.fill(0xff);
  require(rt::detail::format_uuid_v4(bytes) == "FFFFFFFF-FFFF-4FFF-BFFF-FFFFFFFFFFFF",
      "UUID version/variant masking changed");
  std::unordered_set<std::string> identifiers;
  for (unsigned index = 0; index < 64; ++index) {
    const auto id = rt::detail::random_uuid_v4();
    require(id.size() == 36 && id[14] == '4' &&
        (id[19] == '8' || id[19] == '9' || id[19] == 'A' || id[19] == 'B'),
        "System-generated UUID does not use version 4");
    for (std::size_t position = 0; position < id.size(); ++position) {
      const bool separator = position == 8 || position == 13 || position == 18 || position == 23;
      require(separator ? id[position] == '-' :
          ((id[position] >= '0' && id[position] <= '9') || (id[position] >= 'A' && id[position] <= 'F')),
          "System-generated UUID is not canonical text");
    }
    require(identifiers.insert(id).second, "System-generated UUID was repeated");
  }
}

rt::permission_store::clock_type::time_point permission_time{std::chrono::seconds{100}};
rt::permission_store::clock_type::time_point permission_now() { return permission_time; }
bool failed_entropy(unsigned char* output, std::size_t size) {
  std::fill(output, output + size, static_cast<unsigned char>(0x11));
  return false;
}

void test_permissions() {
  rt::permission_store store(&permission_now, dm::mode::scalar);
  std::unordered_set<std::string> tokens;
  for (unsigned index = 0; index < 64; ++index) {
    const auto token = store.issue("session", "app://portable", {"work.run", "bridge.session.renew"});
    require(token.size() == 75 && token.rfind("soui-token-", 0) == 0, "Token format changed");
    require(std::all_of(token.begin() + 11, token.end(), [](unsigned char byte) {
      return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    }), "Token is not lowercase hex");
    require(tokens.insert(token).second, "System entropy produced duplicate tokens");
    require(store.authorize("session", "app://portable", token, "work.run"), "Real token failed authorization");
    require(!store.authorize("other", "app://portable", token, "work.run"), "Session isolation failed");
    require(!store.authorize("session", "app://other", token, "work.run"), "Origin isolation failed");
    require(!store.authorize("session", "app://portable", token, "other"), "Capability isolation failed");
  }

  dm::permissions before, after;
  require(store.read_runtime_metrics(before) == dm::status::ready, "Permission metrics unavailable");
  rt::permission_store_test_access::set_entropy(store, failed_entropy);
  expect_throw<std::runtime_error>([&] { (void)store.issue("failed", "app://portable", {"work.run"}); },
      "Entropy failure issued a token");
  require(store.read_runtime_metrics(after) == dm::status::ready, "Permission failure metrics unavailable");
  require(before.stored_scope_owners == after.stored_scope_owners &&
      before.bearer_alias_entries == after.bearer_alias_entries &&
      before.module_scope_slots_used == after.module_scope_slots_used &&
      after.pending_scope_reservations == 0, "Entropy failure leaked permission capacity");
  rt::permission_store_test_access::restore_entropy(store);

  const auto old_token = *tokens.begin();
  const auto scope = store.resolve("session", "app://portable", old_token, "work.run").scope;
  require(scope != nullptr, "Initial token has no scope");
  require(store.renew("session", "app://portable", old_token, "renew.1").error_code == "E_RENEW_TOO_EARLY",
      "Renewal started before the window");
  permission_time += std::chrono::minutes{25};
  rt::permission_store_test_access::set_entropy(store, failed_entropy);
  require(store.renew("session", "app://portable", old_token, "renew.1").error_code == "E_ENTROPY_UNAVAILABLE",
      "Renewal entropy failure was not surfaced");
  require(store.authorize("session", "app://portable", old_token, "work.run"),
      "Renewal entropy failure revoked the current grant");
  rt::permission_store_test_access::restore_entropy(store);
  const auto renewed = store.renew("session", "app://portable", old_token, "renew.1");
  require(renewed.ok() && renewed.token != old_token && renewed.generation == 2, "Renewal failed");
  require(store.resolve("session", "app://portable", renewed.token, "work.run").scope == scope,
      "Renewal changed stable scope ownership");
  require(store.authorize("session", "app://portable", old_token, "work.run"), "Overlap token rejected early");
  permission_time += std::chrono::seconds{30};
  require(!store.authorize("session", "app://portable", old_token, "work.run"), "Overlap token outlived window");
  require(store.revoke_scope(scope) && !scope->active(), "Scope revocation failed");
  require(!store.authorize("session", "app://portable", renewed.token, "work.run"), "Revoked token remained valid");
  store.revoke_session("session");
}

void test_local_utf8_policy() {
  rt::permission_store store;
  const std::string unicode = "\xe4\xb8\xad\xf0\x9f\x98\x80";
  const auto binding = store.create_local_binding("local", "app://" + unicode);
  const auto issued = store.issue_local(binding, {unicode}, {"work.run"});
  require(store.resolve_local(binding, issued.token, "work.run", unicode).result ==
      rt::authorization_result::authorized, "Unicode local policy was not accepted");
  require(!store.authorize("local", "app://" + unicode, issued.token, unicode),
      "Local token crossed into page authorization");
  for (const auto& invalid : {std::string("\xc0\x80"), std::string("\xed\xa0\x80"),
      std::string("\xf4\x90\x80\x80"), std::string("\xe4\xb8"), std::string("x\0y", 3)}) {
    expect_throw<std::invalid_argument>([&] { (void)store.create_local_binding("bad", "app://" + invalid); },
        "Invalid local origin was accepted");
    expect_throw<std::invalid_argument>([&] { (void)store.issue_local(binding, {invalid}, {"work.run"}); },
        "Invalid local capability was accepted");
  }
  expect_throw<std::invalid_argument>([&] {
    (void)store.issue_local(binding, {std::string(513, 'a')}, {"work.run"});
  }, "Overlong local capability was accepted");
  expect_throw<std::invalid_argument>([&] {
    (void)store.issue_local(binding, {unicode, unicode}, {"work.run"});
  }, "Duplicate local capability was accepted");
  expect_throw<std::invalid_argument>([&] {
    (void)store.create_local_binding("bad", "https://example.test");
  }, "Remote origin was accepted as a local binding");
  binding->close();
  require(!issued.scope->active() &&
      store.resolve_local(binding, issued.token, "work.run", unicode).result == rt::authorization_result::denied,
      "Closed local binding retained authority");
}

void test_metrics_clock() {
  static_assert(std::chrono::steady_clock::is_steady, "Runtime metrics require a monotonic clock");
  auto disabled = dm::detail::begin(dm::mode::disabled, 1);
  dm::detail::end(disabled);
  require(disabled.state == dm::status::disabled && disabled.begin_monotonic_us == 0 &&
      disabled.end_monotonic_us == 0, "Disabled metrics sampled a clock");
  const auto before = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  auto sample = dm::detail::begin(dm::mode::scalar, dm::detail::epoch());
  dm::detail::end(sample);
  const auto after = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  require(before > 0 && after > 0 && sample.state == dm::status::ready && !sample.clock_invalid,
      "Monotonic metrics sample unavailable");
  require(sample.begin_monotonic_us >= static_cast<std::uint64_t>(before) &&
      sample.end_monotonic_us >= sample.begin_monotonic_us &&
      sample.end_monotonic_us <= static_cast<std::uint64_t>(after), "Metric clock is not in steady microseconds");
  bool saturated = false;
  auto counter = (std::numeric_limits<std::uint64_t>::max)() - 1;
  dm::detail::add(counter, 2, saturated);
  require(saturated && counter == (std::numeric_limits<std::uint64_t>::max)(), "Metric counter overflowed");
  require(dm::detail::age(20, 10, sample) == 0 && sample.clock_invalid, "Invalid clock interval was accepted");
}
}  // namespace

int main() {
  try {
    test_utf8();
    test_entropy_reader();
    test_uuid();
    test_permissions();
    test_local_utf8_policy();
    test_metrics_clock();
    std::cout << "Runtime primitives: UTF-8, CSPRNG, UUID, permission lifecycle, and metrics passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Runtime primitives failed: " << error.what() << '\n';
    return 1;
  }
}

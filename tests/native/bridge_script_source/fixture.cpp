#include "bridge_script_source.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace allocation_probe {
thread_local bool enabled = false;
thread_local std::uint64_t calls = 0;
thread_local std::uint64_t bytes = 0;
}

void* operator new(std::size_t bytes) {
  if (void* value = std::malloc(bytes ? bytes : 1)) {
    if (allocation_probe::enabled) {
      ++allocation_probe::calls;
      allocation_probe::bytes += bytes;
    }
    return value;
  }
  throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

#include "legacy-source.inc"

namespace {
namespace current = soui::sdk::detail;
namespace legacy = legacy_bridge_source;
unsigned passed = 0;
volatile std::uint64_t consumed = 0;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template<class Action> void test(const char* name, Action action) {
  try { action(); ++passed; }
  catch (...) { std::cerr << "Failed contract: " << name << '\n'; throw; }
}
std::string repeated(std::string_view seed, std::size_t size) {
  std::string value;
  value.reserve(size);
  while (value.size() < size) value.append(seed.data(), (std::min)(seed.size(), size - value.size()));
  return value;
}
void equivalent(const std::string& id, const std::string& response) {
  require(current::bridge_json_string_literal(id) == legacy::json_string_literal(id), "ID literal bytes differ");
  require(current::bridge_json_string_literal(response) == legacy::json_string_literal(response), "Response literal bytes differ");
  require(current::bridge_resolution_source(id, response) == legacy::bridge_resolution_script(id, response), "Reply source bytes differ");
  require(current::bridge_resolution_source(id, response, true) == legacy::immediate_source(id, response), "Immediate response source bytes differ");
  for (const auto timeout : {1U, 10000U, 60000U}) {
    for (const bool renewal : {false, true})
      require(current::bridge_pending_source(id, timeout, renewal) == legacy::pending_source(id, timeout, renewal), "Pending source bytes differ");
  }
}

void contracts() {
  test("pending expression remains identical", [] {
    const std::string_view before(legacy::detail::create_bridge_pending_script);
    const std::string_view after(current::create_bridge_pending_script, sizeof(current::create_bridge_pending_script) - 1);
    require(before == after && !after.empty() && after.back() == '\n', "Pending body or trailing newline changed");
  });
  test("empty input and null-data view", [] {
    equivalent({}, {});
    require(current::bridge_json_string_literal(std::string_view{}) == "\"\"", "Empty literal changed");
  });
  test("ordinary ASCII runs", [] { equivalent("request-123", repeated("abcdef0123456789 /:=+-", 2048)); });
  test("all 256 bytes together", [] {
    std::string value;
    for (unsigned byte = 0; byte != 256; ++byte) value.push_back(static_cast<char>(byte));
    equivalent(value, value);
  });
  test("each byte at run boundaries", [] {
    for (unsigned byte = 0; byte != 256; ++byte) {
      std::string value("prefix"); value.push_back(static_cast<char>(byte)); value += "suffix";
      equivalent(value, value);
    }
  });
  test("embedded NUL is data", [] { equivalent(std::string("\0id\0", 4), std::string("\0one\0two\0", 9)); });
  test("quotes slashes and named control escapes", [] { equivalent("\\\"\r\n\t", "a\\b\"c\r\nd\te"); });
  test("UTF8 and existing U2028 U2029 behavior", [] {
    const std::string value = u8"中文🙂\u2028\u2029";
    equivalent(value, value);
    require(current::bridge_json_string_literal(value).find("\xe2\x80\xa8") != std::string::npos, "U2028 was newly escaped");
    require(current::bridge_json_string_literal(value).find("\xe2\x80\xa9") != std::string::npos, "U2029 was newly escaped");
  });
  test("512 byte ID", [] { equivalent(repeated("id-\\\"\t", 512), "response"); });
  test("4 KiB response", [] { equivalent("request-4k", repeated("abcdefghijklmnopqrstuvwxyz", 4096)); });
  test("near 64 KiB response", [] { equivalent("request-64k", repeated("0123456789abcdef", 65535)); });
  test("4 KiB escape-heavy response", [] { equivalent("escape-heavy", repeated(std::string_view("\0\r\n\t\\\"", 6), 4096)); });
  test("near 64 KiB escape-heavy response", [] { equivalent("large-escaped", repeated(std::string_view("\1\\\"x", 4), 65535)); });
  test("decimal spelling outside admission is unchanged", [] {
    for (const auto timeout : {0U, (std::numeric_limits<std::uint32_t>::max)()})
      for (const bool renewal : {false, true})
        require(current::bridge_pending_source("id", timeout, renewal) == legacy::pending_source("id", timeout, renewal), "to_string spelling changed");
  });
  test("independent buffers survive nested construction", [] {
    const std::string id(512, 'i'), response(4096, 'r');
    const auto outer = current::bridge_resolution_source(id, response, true);
    const auto* outer_data = outer.data();
    const auto expected = legacy::immediate_source(id, response);
    const auto inner = [&] {
      const auto pending = current::bridge_pending_source("nested", 1, true);
      const auto reply = current::bridge_resolution_source("inner", std::string(65535, 'x'));
      require(pending == legacy::pending_source("nested", 1, true), "Nested pending differs");
      require(outer == expected && outer.data() == outer_data, "Outer buffer overwritten");
      return reply;
    }();
    require(inner == legacy::bridge_resolution_script("inner", std::string(65535, 'x')) && outer == expected, "Live local buffers not independent");
  });
  test("common-size reserve arithmetic rejects overflow", [] {
    std::string output;
    bool first = false, second = false;
    try { current::bridge_source_detail::reserve(output, output.max_size(), "x"); }
    catch (const std::length_error&) { first = true; }
    try { current::bridge_source_detail::reserve(output, output.max_size() - 1, "x", "y"); }
    catch (const std::length_error&) { second = true; }
    require(first && second && output.empty(), "Reserve addition wrapped or allocated huge storage");
  });
}

struct measurement final {
  std::uint64_t allocation_calls{}, requested_allocation_bytes{}, output_bytes{}, checksum{};
  long long elapsed_ns{};
};
constexpr unsigned iterations = 128;

std::uint64_t digest(const std::string& value) {
  return static_cast<std::uint64_t>(value.size()) +
      static_cast<unsigned char>(value.front()) +
      static_cast<unsigned char>(value[value.size() / 2]) +
      static_cast<unsigned char>(value.back());
}
template<class Build> measurement measure(Build build) {
  // One warmup before counting; fixed iterations, no retry-to-target timing loop.
  const auto warm = build();
  const auto expected_size = warm.size();
  const auto expected_digest = digest(warm);
  measurement result;
  allocation_probe::calls = allocation_probe::bytes = 0;
  const auto begin = std::chrono::steady_clock::now();
  allocation_probe::enabled = true;
  try {
    for (unsigned index = 0; index < iterations; ++index) {
      const auto text = build();
      result.output_bytes += text.size();
      result.checksum += digest(text);
    }
  } catch (...) { allocation_probe::enabled = false; throw; }
  allocation_probe::enabled = false;
  const auto end = std::chrono::steady_clock::now();
  result.allocation_calls = allocation_probe::calls;
  result.requested_allocation_bytes = allocation_probe::bytes;
  result.elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
  consumed = result.checksum;
  require(result.output_bytes == expected_size * iterations && result.checksum == expected_digest * iterations, "CPU slice did not perform fixed output work");
  return result;
}
void print(const measurement& value) {
  std::cout << "{\"allocationCalls\":" << value.allocation_calls
            << ",\"requestedAllocationBytes\":" << value.requested_allocation_bytes
            << ",\"outputBytes\":" << value.output_bytes
            << ",\"checksum\":" << value.checksum
            << ",\"elapsedNs\":" << value.elapsed_ns << '}';
}
template<class Before, class After> void slice(const char* name, Before before, After after, bool comma) {
  require(before() == after(), "Benchmark output differs");
  const auto old_value = measure(before), new_value = measure(after);
  require(old_value.output_bytes == new_value.output_bytes && old_value.checksum == new_value.checksum, "Benchmark work differs");
  if (comma) std::cout << ',';
  std::cout << "{\"name\":\"" << name << "\",\"iterations\":" << iterations << ",\"legacy\":";
  print(old_value); std::cout << ",\"current\":"; print(new_value);
  std::cout << ",\"fewerAllocationsObserved\":" << (new_value.allocation_calls < old_value.allocation_calls ? "true" : "false") << '}';
}
void benchmark() {
  const std::string id(512, 'i'), small(4096, 'x'), large(65535, 'x');
  const auto escaped = repeated(std::string_view("\1\\\"x", 4), 4096);
  std::cout << ",\"cpuSlices\":[";
  slice("pending-512-id", [&] { return legacy::pending_source(id, 60000, false); },
      [&] { return current::bridge_pending_source(id, 60000, false); }, false);
  slice("reply-4k", [&] { return legacy::bridge_resolution_script(id, small); },
      [&] { return current::bridge_resolution_source(id, small); }, true);
  slice("reply-near64k", [&] { return legacy::bridge_resolution_script(id, large); },
      [&] { return current::bridge_resolution_source(id, large); }, true);
  slice("immediate-4k-escaped", [&] { return legacy::immediate_source(id, escaped); },
      [&] { return current::bridge_resolution_source(id, escaped, true); }, true);
  std::cout << ']';
}
}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--benchmark"), "Expected no argument or --benchmark");
    contracts();
    std::cout << "{\"schema\":\"soui.bridge-script-source-contracts/v1\",\"passed\":" << passed
              << ",\"guiExecuted\":false,\"jsExecuted\":false,\"apiStageSpeedupMeasured\":false";
    if (argc == 2) benchmark();
    std::cout << "}\n";
    return 0;
  } catch (const std::exception& error) {
    allocation_probe::enabled = false;
    std::cerr << error.what() << '\n';
    return 1;
  }
}

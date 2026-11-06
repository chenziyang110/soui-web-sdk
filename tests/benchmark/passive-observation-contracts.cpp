// CPU-only fixture. No window, graphics device or input is created. The timer
// contract starts only this same EXE's fixed no-window child, which exits 0.
// Root may compile this standalone; it adds no project target/translation unit.
#include "../../benchmarks/protocol/passive-wait-timer.h"
#include <atomic>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

namespace {
using namespace soui::benchmark;
constexpr std::uint32_t nonce = 0x12345678;
void check(bool value) { if (!value) throw std::runtime_error("contract assertion failed"); }
void rejects(const std::function<void()>& action, const char* code = nullptr) {
  try { action(); } catch (const std::exception& error) { if (code) check(std::string(error.what()) == code); return; }
  throw std::runtime_error("invalid input was accepted");
}
struct test_clock {
  std::uint64_t last{}, frequency{};
  test_clock() { LARGE_INTEGER q{}; check(QueryPerformanceFrequency(&q) && q.QuadPart > 0); frequency = static_cast<std::uint64_t>(q.QuadPart); }
  std::uint64_t read() {
    LARGE_INTEGER q{}; check(QueryPerformanceCounter(&q) && q.QuadPart > 0);
    const auto now = static_cast<std::uint64_t>(q.QuadPart); check(now >= last); return last = now;
  }
  std::uint64_t deadline(unsigned ms) {
    check(frequency <= std::numeric_limits<std::uint64_t>::max() / ms);
    const auto now = read(), duration = frequency * ms / 1000;
    check(duration && now <= std::numeric_limits<std::uint64_t>::max() - duration); return now + duration;
  }
};
struct test_handle {
  HANDLE value{};
  ~test_handle() { if (value) CloseHandle(value); }
  void duplicate_current_process() {
    const HANDLE current = GetCurrentProcess();
    check(!value && DuplicateHandle(current, current, current, &value, 0, FALSE, DUPLICATE_SAME_ACCESS));
    check(value && GetProcessId(value) == GetCurrentProcessId());
  }
  void close() { const HANDLE closing = value; value = nullptr; check(closing && CloseHandle(closing)); }
};
std::uint64_t filetime(FILETIME value) { return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
struct timer_facts {
  std::string selected, fallback;
  std::uint64_t frequency{}, message_deadline{}, message_seen{}, message_ended{}, child_creation{};
  unsigned messages{}, child_pid{};
} timers;
struct arguments {
  std::vector<std::wstring> values{L"InputTrace", L"--observe-manual", L"123", L"456", L"789",
      L"11111111111111111111111112345678", L"8", L"1000", L"1000", L"C:/passive-new.ndjson"};
  passive_options parse() {
    std::vector<wchar_t*> pointers; for (auto& value : values) pointers.push_back(value.data());
    return parse_passive_options(static_cast<int>(pointers.size()), pointers.data());
  }
};
std::array<unsigned char, 16> wire(unsigned revision = 1, unsigned frame = 1, unsigned epoch = 1, unsigned flags = 0) {
  std::array<unsigned char, 16> bytes{0xa7, 0x4d};
  const auto put = [&bytes](unsigned first, unsigned count, std::uint32_t value) {
    for (unsigned n = 0; n < count; ++n) bytes[first + n] = static_cast<unsigned char>(value >> (n * 8));
  };
  put(2, 4, nonce); put(6, 3, revision); put(9, 2, frame); bytes[11] = static_cast<unsigned char>(flags); put(14, 2, epoch);
  put(12, 2, passive_crc16(bytes)); return bytes;
}
std::vector<unsigned char> image(const std::array<unsigned char, 16>& bytes, unsigned revision, std::size_t pitch = 256) {
  std::vector<unsigned char> pixels(pitch * 40, 0x5a);
  for (unsigned y = 0; y < 40; ++y) for (unsigned x = 0; x < 64; ++x) {
    auto* pixel = pixels.data() + y * pitch + x * 4;
    pixel[0] = static_cast<unsigned char>(29 * revision); pixel[1] = static_cast<unsigned char>(151 * revision);
    pixel[2] = static_cast<unsigned char>(73 * revision); pixel[3] = 255;
  }
  for (unsigned bit = 0; bit < 128; ++bit) for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
    auto* pixel = pixels.data() + (8 + (bit / 16) * 4 + y) * pitch + ((bit % 16) * 4 + x) * 4;
    pixel[0] = pixel[1] = pixel[2] = bytes[bit / 8] & (1U << (bit % 8)) ? 255 : 0;
  }
  return pixels;
}
passive_pixels value(unsigned revision = 1, unsigned frame = 1, unsigned epoch = 1) {
  const auto pixels = image(wire(revision, frame, epoch), revision);
  auto result = decode_passive_pixels(pixels.data(), 256, nonce); check(!result.reason); return result;
}
} // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--passive-wait-child") { Sleep(50); return 0; }
  if (argc != 1) return 2;
  std::vector<std::pair<const char*, std::function<void()>>> cases{
    {"explicit passive options", [] { arguments a; const auto o = a.parse(); check(o.pid == 123 && o.hwnd == 789 && o.interval_ms == 8 && passive_nonce(o.run) == nonce); }},
    {"unknown mode never accepted", [] { arguments a; a.values[1] = L"--observe-manua"; rejects([&] { a.parse(); }); }},
    {"wrong arity", [] { arguments a; a.values.pop_back(); rejects([&] { a.parse(); }); }},
    {"zero and noncanonical PID", [] { for (const auto* text : {L"0", L"01", L"+1", L"-1", L"1x", L"4294967296"}) { arguments a; a.values[2] = text; rejects([&] { a.parse(); }); } }},
    {"identity overflow", [] { for (unsigned field : {3U, 4U}) { arguments a; a.values[field] = L"18446744073709551616"; rejects([&] { a.parse(); }); } }},
    {"full run format", [] { for (const auto* run : {L"00000000000000000000000000000000", L"1111111111111111111111111234567A", L"123"}) { arguments a; a.values[5] = run; rejects([&] { a.parse(); }); } }},
    {"run suffix zero maps to nonce one", [] { check(passive_nonce("11111111111111111111111100000000") == 1); }},
    {"interval and finite budgets", [] { for (const auto pair : {std::pair<unsigned, const wchar_t*>{6, L"1001"}, {7, L"999"}, {7, L"120001"}, {8, L"999"}, {8, L"60001"}}) { arguments a; a.values[pair.first] = pair.second; rejects([&] { a.parse(); }); } }},
    {"absolute new output contract", [] { arguments a; a.values[9] = L"relative.ndjson"; rejects([&] { a.parse(); }); }},
    {"relative timer ceil and overflow fail closed", [] {
      std::int64_t due{};
      check(passive_relative_due_100ns(1, 3, due) && due == -3333334);
      check(passive_relative_due_100ns(1, 10000000, due) && due == -1);
      check(passive_relative_due_100ns(20000001, 10000000, due) && due == -20000001);
      constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
      check(passive_relative_due_100ns(maximum, 10000000, due) && due == -std::numeric_limits<std::int64_t>::max());
      for (const auto pair : {std::pair<std::uint64_t, std::uint64_t>{0, 1}, {1, 0}, {maximum + 1, 10000000},
           {std::numeric_limits<std::uint64_t>::max() - 1, std::numeric_limits<std::uint64_t>::max()}}) {
        due = -1; check(!passive_relative_due_100ns(pair.first, pair.second, due) && due == 0);
      }
    }},
    {"real timer selected and standard wait cancel close", [] {
      test_handle process; process.duplicate_current_process();
      test_clock clock; timers.frequency = clock.frequency;
      for (bool prefer_high_resolution : {true, false}) {
        passive_wait_timer timer; timer.create(prefer_high_resolution);
        (prefer_high_resolution ? timers.selected : timers.fallback) = timer.backend();
        if (!prefer_high_resolution) check(!timer.high_resolution);
        timer.arm(clock.frequency, clock.frequency); check(timer.armed);
        timer.cancel(); check(!timer.armed);
        const auto until = clock.deadline(20);
        passive_wait_until(process.value, clock, timer, until, clock.frequency, [] {});
        check(clock.read() >= until && !timer.armed);
        timer.close(); check(!timer.timer && !timer.armed);
      }
      process.close();
    }},
    {"thread message wake keeps absolute deadline", [] {
      test_handle process; process.duplicate_current_process();
      test_clock clock; passive_wait_timer timer; timer.create();
      constexpr UINT message = WM_APP + 0x337;
      MSG item{}; PeekMessageW(&item, nullptr, message, message, PM_NOREMOVE); // Establish only this thread's queue.
      const DWORD thread = GetCurrentThreadId();
      const auto until = clock.deadline(750); timers.message_deadline = until;
      std::atomic<bool> posted{};
      std::thread sender([thread, &posted] { Sleep(50); posted.store(PostThreadMessageW(thread, message, 0, 0) != FALSE); });
      try {
        passive_wait_until(process.value, clock, timer, until, clock.frequency, [&] {
          unsigned batch{};
          while (PeekMessageW(&item, nullptr, message, message, PM_REMOVE)) {
            check(++batch <= 64 && item.hwnd == nullptr && item.message == message);
            ++timers.messages; timers.message_seen = clock.read();
          }
        });
      } catch (...) { sender.join(); throw; }
      sender.join(); timers.message_ended = clock.read();
      check(posted.load() && timers.messages == 1 && timers.message_seen < until && timers.message_ended >= until);
      timer.close();
      process.close();
    }},
    {"held no-window process exits before timer deadline", [] {
      std::wstring executable(32768, L'\0');
      const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
      check(length && length < executable.size()); executable.resize(length);
      std::wstring command = L"\"" + executable + L"\" --passive-wait-child";
      STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
      check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child));
      test_handle process{child.hProcess}, thread{child.hThread}; thread.close();
      FILETIME creation{}, exited{}, kernel{}, user{};
      check(GetProcessId(process.value) == child.dwProcessId && GetProcessTimes(process.value, &creation, &exited, &kernel, &user));
      timers.child_pid = child.dwProcessId; timers.child_creation = filetime(creation);
      test_clock clock; passive_wait_timer timer; timer.create(); const auto until = clock.deadline(5000);
      rejects([&] { passive_wait_until(process.value, clock, timer, until, clock.frequency, [] {}); }, "target-closed");
      DWORD code{};
      check(clock.read() < until && WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0 &&
          GetExitCodeProcess(process.value, &code) && code == 0 && GetProcessTimes(process.value, &creation, &exited, &kernel, &user) &&
          filetime(creation) == timers.child_creation);
      timer.close(); process.close();
    }},
    {"manual marker and epoch decode", [] { const auto p = value(123, 45, 67); check(p.revision == 123 && p.frame == 45 && p.epoch == 67 && p.marker_hex().size() == 32); }},
    {"epoch is CRC covered", [] { auto bytes = wire(); bytes[14] = 2; const auto pixels = image(bytes, 1); check(std::string(decode_passive_pixels(pixels.data(), 256, nonce).reason) == "marker-crc"); }},
    {"nonce is a local run check", [] { const auto pixels = image(wire(), 1); check(std::string(decode_passive_pixels(pixels.data(), 256, 2).reason) == "marker-nonce"); }},
    {"zero epoch cannot be baseline", [] { const auto pixels = image(wire(1, 1, 0), 1); check(std::string(decode_passive_pixels(pixels.data(), 256, nonce).reason) == "marker-epoch"); }},
    {"overflow and geometry flags reject", [] { for (unsigned flags : {1U, 2U, 4U}) { const auto pixels = image(wire(1, 1, 1, flags), 1); check(std::string(decode_passive_pixels(pixels.data(), 256, nonce).reason) == "marker-flags"); } }},
    {"counter capacity and initial frame", [] { for (const auto pair : {std::pair<unsigned, unsigned>{1U << 20, 1}, {1, 0}}) { const auto pixels = image(wire(pair.first, pair.second), pair.first); check(std::string(decode_passive_pixels(pixels.data(), 256, nonce).reason) == "marker-counter"); } }},
    {"ambiguous marker is retained as unreadable", [] { auto pixels = image(wire(), 1); const auto at = 10 * 256 + 2 * 4; pixels[at] = pixels[at + 1] = pixels[at + 2] = 100; const auto p = decode_passive_pixels(pixels.data(), 256, nonce); check(!p.marker_readable && std::string(p.reason) == "marker-pixels"); }},
    {"independent original content probe", [] { auto pixels = image(wire(), 1); pixels[2 * 256 + 2 * 4] ^= 1; check(std::string(decode_passive_pixels(pixels.data(), 256, nonce).reason) == "probe-mismatch"); }},
    {"CRC32 excludes pitch padding", [] { const auto a = image(wire(), 1), b = image(wire(), 1, 272); check(decode_passive_pixels(a.data(), 256, nonce).crc32 == decode_passive_pixels(b.data(), 272, nonce).crc32); }},
    {"buffer bounds", [] { rejects([] { decode_passive_pixels(nullptr, 256, nonce); }, "capture-failed"); unsigned char p{}; rejects([&] { decode_passive_pixels(&p, 255, nonce); }, "capture-failed"); }},
    {"duplicate content not a frame", [] { passive_sequence s; s.accept(value()); const auto t = s.accept(value()); check(t.duplicate && !t.content_change && !t.marker_only); }},
    {"frame-only marker is not effective content", [] { passive_sequence s; s.accept(value()); const auto t = s.accept(value(1, 2)); check(t.marker_only && !t.content_change && !t.reason); }},
    {"coalesced local feedback is not native input ID", [] { passive_sequence s; s.accept(value()); const auto t = s.accept(value(3, 2)); check(t.content_change && t.revision_delta == 2 && !t.frame_gap); }},
    {"256-period alias cannot count", [] { passive_sequence s; s.accept(value()); const auto t = s.accept(value(257, 3)); check(t.frame_gap == 1 && t.marker_only && !t.content_change && std::string(t.reason) == "content-alias"); }},
    // CRC/probe-valid endpoints can skip local renders before one desktop
    // update. DXGI AccumulatedFrames is checked separately by the collector.
    {"unobserved page render is not desktop loss", [] { passive_sequence s; s.accept(value()); const auto t = s.accept(value(2, 3)); check(t.frame_gap == 1 && t.content_change && !t.reason); }},
    {"counter regression terminates", [] { passive_sequence s; s.accept(value(2, 2)); rejects([&] { s.accept(value(1, 3)); }, "counter-regressed"); }},
    {"same frame cannot change revision", [] { passive_sequence s; s.accept(value()); rejects([&] { s.accept(value(2, 1)); }, "counter-regressed"); }},
    {"reload epoch cannot merge", [] { passive_sequence s; s.accept(value()); rejects([&] { s.accept(value(2, 2, 2)); }, "epoch-changed"); }}
  };
  unsigned passed{};
  for (const auto& test : cases) {
    try { test.second(); ++passed; }
    catch (const std::exception& error) { std::cerr << test.first << ": " << error.what() << '\n'; return 1; }
  }
  std::cout << "{\"schema\":\"soui.passive-observation-contracts/v2\",\"passed\":" << passed
      << ",\"waitTimerBackend\":\"" << timers.selected << "\",\"forcedFallbackBackend\":\"" << timers.fallback
      << "\",\"qpcFrequency\":\"" << timers.frequency << "\",\"messageDeadlineQpc\":\"" << timers.message_deadline
      << "\",\"messageSeenQpc\":\"" << timers.message_seen << "\",\"messageEndedQpc\":\"" << timers.message_ended
      << "\",\"messages\":" << timers.messages << ",\"childPid\":" << timers.child_pid
      << ",\"childCreationFileTime\":\"" << timers.child_creation
      << "\",\"childExitCode\":0,\"childProcessesCreated\":1,\"guiExecuted\":false,\"inputInjected\":false}\n";
  return 0;
}

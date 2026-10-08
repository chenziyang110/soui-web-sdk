#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
struct file_handle {
  HANDLE value{INVALID_HANDLE_VALUE};
  explicit file_handle(HANDLE input) : value(input) {}
  ~file_handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  file_handle(const file_handle&) = delete;
  file_handle& operator=(const file_handle&) = delete;
};

struct window_search { DWORD pid; HWND found{nullptr}; };
BOOL CALLBACK find_window(HWND window, LPARAM data) {
  auto& search = *reinterpret_cast<window_search*>(data);
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid != search.pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
  RECT client{};
  if (!GetClientRect(window, &client) || client.right - client.left != 1100 ||
      client.bottom - client.top != 720) return TRUE;
  search.found = window;
  return FALSE;
}

std::uint64_t qpc() {
  LARGE_INTEGER value{};
  if (!QueryPerformanceCounter(&value)) throw std::runtime_error("QPC unavailable");
  return static_cast<std::uint64_t>(value.QuadPart);
}

std::uint64_t filetime(FILETIME value) {
  return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

void write_line(HANDLE file, const std::string& line) {
  DWORD written = 0;
  if (!WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) ||
      written != line.size() || !FlushFileBuffers(file))
    throw std::runtime_error("Cannot persist input trace");
}

DWORD parse_pid(const wchar_t* text) {
  if (!text || !*text) throw std::runtime_error("Missing PID");
  std::uint64_t number = 0;
  for (const wchar_t* next = text; *next; ++next) {
    if (*next < L'0' || *next > L'9') throw std::runtime_error("Invalid PID");
    const auto digit = static_cast<unsigned>(*next - L'0');
    if (number > (std::numeric_limits<DWORD>::max() - digit) / 10U)
      throw std::runtime_error("PID exceeds limit");
    number = number * 10U + digit;
  }
  if (!number) throw std::runtime_error("PID must be positive");
  return static_cast<DWORD>(number);
}

class trace final {
 public:
  trace(HANDLE file, HANDLE process, DWORD pid, HWND window)
      : file_(file), process_(process), pid_(pid), window_(window), started_(GetTickCount64()) {}
  std::uint64_t elapsed() const { return GetTickCount64() - started_; }
  void wait_until(std::uint64_t target) const {
    while (elapsed() < target) {
      const auto remaining = target - elapsed();
      Sleep(static_cast<DWORD>(remaining > 20 ? 20 : remaining));
    }
  }
  void record(const char* action, int x, int y, std::uint64_t before, std::uint64_t after) {
    write_line(file_, "{\"event\":\"input\",\"action\":\"" + std::string(action) +
        "\",\"elapsedMs\":" + std::to_string(elapsed()) +
        ",\"qpcBefore\":" + std::to_string(before) +
        ",\"qpcAfter\":" + std::to_string(after) +
        ",\"clientX\":" + std::to_string(x) +
        ",\"clientY\":" + std::to_string(y) + "}\n");
    ++actions_;
  }
  void foreground() const {
    DWORD current_pid = 0;
    GetWindowThreadProcessId(window_, &current_pid);
    if (!IsWindow(window_) || current_pid != pid_ ||
        WaitForSingleObject(process_, 0) != WAIT_TIMEOUT)
      throw std::runtime_error("Benchmark window or process closed");
    if (GetForegroundWindow() == window_) return;
    ShowWindow(window_, SW_RESTORE);
    BringWindowToTop(window_);
    SetForegroundWindow(window_);
    Sleep(50);
    if (GetForegroundWindow() != window_) throw std::runtime_error("Benchmark window is not foreground");
  }
  void cursor(int x, int y) const {
    foreground();
    RECT area{};
    if (!GetClientRect(window_, &area) || x < 0 || y < 0 || x >= area.right || y >= area.bottom)
      throw std::runtime_error("Input coordinate is outside the content area");
    POINT point{x, y};
    if (!ClientToScreen(window_, &point) || !SetCursorPos(point.x, point.y))
      throw std::runtime_error("Cannot position input pointer");
  }
  void click(int x, int y, const char* name) {
    cursor(x, y);
    INPUT inputs[2]{};
    for (auto& input : inputs) input.type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    const auto before = qpc();
    if (SendInput(2, inputs, sizeof(INPUT)) != 2) throw std::runtime_error("Click injection failed");
    record(name, x, y, before, qpc());
  }
  void wheel(int x, int y, int amount) {
    cursor(x, y);
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(amount);
    const auto before = qpc();
    if (SendInput(1, &input, sizeof(INPUT)) != 1) throw std::runtime_error("Wheel injection failed");
    record(amount > 0 ? "wheel-up" : "wheel-down", x, y, before, qpc());
  }
  void unicode(wchar_t character) {
    foreground();
    INPUT inputs[2]{};
    for (auto& input : inputs) {
      input.type = INPUT_KEYBOARD;
      input.ki.wScan = character;
      input.ki.dwFlags = KEYEVENTF_UNICODE;
    }
    inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    const auto before = qpc();
    if (SendInput(2, inputs, sizeof(INPUT)) != 2) throw std::runtime_error("Unicode injection failed");
    record("unicode-key-not-ime", -1, -1, before, qpc());
  }
  void resize(int width, int height) {
    foreground();
    const auto style = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
    const auto extended = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE));
    RECT outer{0, 0, width, height};
    if (!AdjustWindowRectEx(&outer, style, GetMenu(window_) != nullptr, extended))
      throw std::runtime_error("Cannot calculate benchmark window frame");
    const auto before = qpc();
    if (!SetWindowPos(window_, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
      throw std::runtime_error("Cannot resize benchmark window");
    RECT actual{};
    if (!GetClientRect(window_, &actual) || actual.right != width || actual.bottom != height)
      throw std::runtime_error("Benchmark content resize differs from requested size");
    record("window-resize", width, height, before, qpc());
  }
  unsigned actions() const { return actions_; }
 private:
  HANDLE file_;
  HANDLE process_;
  DWORD pid_;
  HWND window_;
  std::uint64_t started_;
  unsigned actions_{0};
};

int run(int argc, wchar_t** argv) {
  if (argc != 5) throw std::runtime_error("Usage: InputTrace PID S01|S02|S03|S04|S05 ABSOLUTE_NEW_OUTPUT.ndjson full|probe");
  const DWORD pid = parse_pid(argv[1]);
  const std::wstring scene(argv[2]);
  if (scene != L"S01" && scene != L"S02" && scene != L"S03" && scene != L"S04" && scene != L"S05")
    throw std::runtime_error("Unknown benchmark scene");
  const std::filesystem::path output(argv[3]);
  if (!output.is_absolute()) throw std::runtime_error("An absolute output path is required");
  // The last argument is an explicit test-only switch. A future real run must
  // opt out of the short path by passing the word 'full'.
  const std::wstring duration_mode(argv[4]);
  if (duration_mode != L"full" && duration_mode != L"probe")
    throw std::runtime_error("Expected full or probe duration mode");
  file_handle process{OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid)};
  if (!process.value) throw std::runtime_error("Benchmark process is unavailable");
  FILETIME creation{}, process_exit{}, kernel{}, user{};
  if (!GetProcessTimes(process.value, &creation, &process_exit, &kernel, &user))
    throw std::runtime_error("Cannot read benchmark process identity");
  window_search search{pid};
  const auto window_deadline = GetTickCount64() + 45000;
  while (!search.found && GetTickCount64() < window_deadline) {
    if (WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT)
      throw std::runtime_error("Benchmark process exited before its window appeared");
    EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
    if (!search.found) Sleep(100);
  }
  if (!search.found) throw std::runtime_error("A visible 1100 x 720 benchmark window is required");
  const int screen_width = GetSystemMetrics(SM_CXSCREEN);
  const int screen_height = GetSystemMetrics(SM_CYSCREEN);
  RECT outer{};
  if (!GetWindowRect(search.found, &outer) || outer.right - outer.left > screen_width ||
      outer.bottom - outer.top > screen_height) throw std::runtime_error("The benchmark window does not fit on the primary display");
  const auto position_x = (screen_width - (outer.right - outer.left)) / 2;
  const auto position_y = (screen_height - (outer.bottom - outer.top)) / 2;
  if (!SetWindowPos(search.found, HWND_TOP, position_x, position_y, 0, 0,
                    SWP_NOSIZE | SWP_SHOWWINDOW)) throw std::runtime_error("Cannot position benchmark window");
  file_handle file{CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create new input trace");
  LARGE_INTEGER frequency{};
  if (!QueryPerformanceFrequency(&frequency)) throw std::runtime_error("QPC frequency unavailable");
  FILETIME utc{};
  GetSystemTimeAsFileTime(&utc);
  std::string scene_name{"S0"};
  scene_name.push_back(static_cast<char>(scene[2]));
  write_line(file.value, "{\"event\":\"run.begin\",\"schema\":\"soui.external-input-trace/v1\",\"pid\":" +
      std::to_string(pid) + ",\"scene\":\"" + scene_name + "\",\"contentWidth\":1100,\"contentHeight\":720" +
      ",\"processCreationTime100ns\":" + std::to_string(filetime(creation)) +
      ",\"qpcFrequency\":" + std::to_string(frequency.QuadPart) +
      ",\"startedAtUtcFiletime100ns\":" + std::to_string(filetime(utc)) +
      ",\"mode\":\"" + std::string(duration_mode == L"full" ? "full" : "probe") + "\"}\n");
  trace input(file.value, process.value, pid, search.found);
  input.foreground();
  const int tab_x = 47 + 72 * static_cast<int>(scene[2] - L'1');
  input.click(tab_x, 654, "select-scene");
  input.wait_until(150);
  input.click(tab_x, 654, "select-scene-confirm");
  input.wait_until(800);
  const std::uint64_t duration = duration_mode == L"probe" ? 3000 : (scene == L"S05" ? 123000 : 61000);
  if (scene == L"S05") input.click(53, 698, "start-motion");
  else if (scene == L"S04") input.click(53, 698, "arm-window");
  unsigned tick = 0;
  while (input.elapsed() < duration) {
    if (scene == L"S01") input.click(tick % 2 ? 137 : 53, 698, tick % 2 ? "expand" : "switch");
    else if (scene == L"S02") {
      input.wheel(550, 320, tick % 8 < 4 ? -120 : 120);
      if (tick % 12 == 11) input.click(53, 698, "reset-scroll");
    } else if (scene == L"S03") {
      input.click(53, 698, "open-menu");
      input.click(221, 698, "focus-input");
      Sleep(100);
      input.unicode(L'中');
    } else if (scene == L"S04") input.resize(tick % 2 ? 1100 : 1060, tick % 2 ? 720 : 700);
    ++tick;
    input.wait_until(800 + static_cast<std::uint64_t>(tick) * 1000);
  }
  if (scene == L"S04") input.resize(1100, 720);
  input.click(1040, 698, "finish-scene");
  write_line(file.value, "{\"event\":\"run.end\",\"elapsedMs\":" +
      std::to_string(input.elapsed()) + ",\"actions\":" + std::to_string(input.actions()) +
      ",\"imeCompositionCollected\":false}\n");
  return 0;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetProcessDPIAware();
  try { return run(argc, argv); }
  catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}

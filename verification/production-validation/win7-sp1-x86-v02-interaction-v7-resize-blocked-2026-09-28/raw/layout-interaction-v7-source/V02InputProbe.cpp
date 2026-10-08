#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
struct process_handle {
  HANDLE value{nullptr};
  explicit process_handle(DWORD pid) : value(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid)) {
    if (!value) throw std::runtime_error("target process unavailable");
  }
  ~process_handle() { CloseHandle(value); }
  process_handle(const process_handle&) = delete;
  process_handle& operator=(const process_handle&) = delete;
};

struct window_search { DWORD pid; HWND window{nullptr}; };
BOOL CALLBACK find_window(HWND hwnd, LPARAM data) {
  auto& search = *reinterpret_cast<window_search*>(data);
  DWORD owner = 0;
  GetWindowThreadProcessId(hwnd, &owner);
  if (owner != search.pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
  RECT rect{};
  if (!GetWindowRect(hwnd, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) return TRUE;
  search.window = hwnd;
  return FALSE;
}

DWORD parse_positive(const wchar_t* text) {
  if (!text || !*text) throw std::runtime_error("missing positive integer");
  std::uint64_t value = 0;
  for (const wchar_t* at = text; *at; ++at) {
    if (*at < L'0' || *at > L'9') throw std::runtime_error("invalid positive integer");
    const unsigned digit = static_cast<unsigned>(*at - L'0');
    if (value > (std::numeric_limits<DWORD>::max() - digit) / 10U)
      throw std::runtime_error("integer exceeds DWORD range");
    value = value * 10U + digit;
  }
  if (!value) throw std::runtime_error("integer must be nonzero");
  return static_cast<DWORD>(value);
}

std::uint64_t as_u64(FILETIME value) {
  return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

void require_foreground(HWND window, DWORD pid, HANDLE process) {
  DWORD owner = 0;
  GetWindowThreadProcessId(window, &owner);
  if (!IsWindow(window) || !IsWindowVisible(window) || owner != pid ||
      WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
    throw std::runtime_error("target window or process changed");
  if (GetForegroundWindow() != window)
    throw std::runtime_error("target window is not naturally foreground");
}

void move_to_client(HWND window, int x, int y) {
  RECT client{};
  if (!GetClientRect(window, &client) || x < 0 || y < 0 || x >= client.right || y >= client.bottom)
    throw std::runtime_error("point outside target client area");
  POINT point{x, y};
  if (!ClientToScreen(window, &point))
    throw std::runtime_error("cannot map input point to screen");
  const HWND hit = WindowFromPoint(point);
  if (!hit || GetAncestor(hit, GA_ROOT) != window)
    throw std::runtime_error("input point is covered by another window");
  if (!SetCursorPos(point.x, point.y))
    throw std::runtime_error("cannot position pointer");
}

void mouse(DWORD flags, DWORD data = 0) {
  INPUT input{};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = flags;
  input.mi.mouseData = data;
  if (SendInput(1, &input, sizeof(input)) != 1) throw std::runtime_error("mouse input failed");
}

void key(WORD virtual_key) {
  INPUT input[2]{};
  for (auto& part : input) {
    part.type = INPUT_KEYBOARD;
    part.ki.wVk = virtual_key;
  }
  input[1].ki.dwFlags = KEYEVENTF_KEYUP;
  if (SendInput(2, input, sizeof(INPUT)) != 2) throw std::runtime_error("key input failed");
}

int run(int argc, wchar_t** argv) {
  if (argc != 6) throw std::runtime_error(
      "usage: V02InputProbe PID EXPECTED_CREATION_FILETIME inspect|click|wheel|wheel-up|pinyin|resize-drag ARG ABSOLUTE_NEW_RESULT.json");
  const DWORD pid = parse_positive(argv[1]);
  const std::wstring creation_text = argv[2];
  if (creation_text.empty() || creation_text.find_first_not_of(L"0123456789") != std::wstring::npos)
    throw std::runtime_error("invalid creation identity");
  const std::uint64_t expected_creation = std::stoull(creation_text);
  const std::wstring action = argv[3];
  const std::wstring argument = argv[4];
  const std::wstring output = argv[5];
  if (output.size() < 4 || output[1] != L':' || output[2] != L'\\')
    throw std::runtime_error("output must be an absolute Windows path");
  if (GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
    throw std::runtime_error("output already exists");
  process_handle process(pid);
  FILETIME creation{}, exit{}, kernel{}, user{};
  if (!GetProcessTimes(process.value, &creation, &exit, &kernel, &user))
    throw std::runtime_error("target process creation identity unavailable");
  const std::uint64_t actual_creation = as_u64(creation);
  if ((expected_creation == 0 && action != L"inspect") ||
      (expected_creation != 0 && actual_creation != expected_creation))
    throw std::runtime_error("target process creation identity differs");
  window_search search{pid};
  EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
  if (!search.window) throw std::runtime_error("visible unowned top-level target window missing");
  RECT before{};
  if (!GetWindowRect(search.window, &before)) throw std::runtime_error("window rectangle unavailable");
  const bool foreground = GetForegroundWindow() == search.window;
  if (action != L"inspect") require_foreground(search.window, pid, process.value);
  if (action == L"inspect") {
    if (argument != L"-") throw std::runtime_error("inspect argument must be -");
  } else if (action == L"click" || action == L"wheel" || action == L"wheel-up") {
    const auto separator = argument.find(L',');
    if (separator == std::wstring::npos) throw std::runtime_error("expected client x,y");
    const int x = std::stoi(argument.substr(0, separator));
    const int y = std::stoi(argument.substr(separator + 1));
    move_to_client(search.window, x, y);
    require_foreground(search.window, pid, process.value);
    if (action == L"click") { mouse(MOUSEEVENTF_LEFTDOWN); mouse(MOUSEEVENTF_LEFTUP); }
    else mouse(MOUSEEVENTF_WHEEL,
               static_cast<DWORD>(action == L"wheel-up" ? WHEEL_DELTA : -WHEEL_DELTA));
  } else if (action == L"pinyin") {
    if (argument.empty() || argument.size() > 32) throw std::runtime_error("invalid pinyin length");
    for (const wchar_t letter : argument) {
      require_foreground(search.window, pid, process.value);
      if (letter < L'a' || letter > L'z') throw std::runtime_error("pinyin must use lowercase ASCII letters");
      key(static_cast<WORD>(L'A' + letter - L'a'));
      Sleep(40);
    }
    key(VK_SPACE);
  } else if (action == L"resize-drag") {
    if (argument != L"80,60") throw std::runtime_error("resize-drag requires frozen 80,60 delta");
    POINT start{before.right - 4, before.bottom - 4};
    const HWND hit = WindowFromPoint(start);
    if (!hit || GetAncestor(hit, GA_ROOT) != search.window)
      throw std::runtime_error("resize border is covered by another window");
    if (!SetCursorPos(start.x, start.y)) throw std::runtime_error("cannot position resize pointer");
    require_foreground(search.window, pid, process.value);
    mouse(MOUSEEVENTF_LEFTDOWN);
    for (int step = 1; step <= 8; ++step) {
      if (!SetCursorPos(start.x + step * 10, start.y + step * 7 + step / 2))
        throw std::runtime_error("cannot drag resize pointer");
      Sleep(40);
    }
    mouse(MOUSEEVENTF_LEFTUP);
    Sleep(100);
  } else throw std::runtime_error("unknown action");
  RECT after{};
  if (!GetWindowRect(search.window, &after)) throw std::runtime_error("window closed during action");
  if (action == L"resize-drag" &&
      (after.right - after.left <= before.right - before.left ||
       after.bottom - after.top <= before.bottom - before.top))
    throw std::runtime_error("resize drag did not increase both window dimensions");
  if (action != L"inspect") require_foreground(search.window, pid, process.value);
  GUITHREADINFO gui{sizeof(gui)};
  const DWORD thread = GetWindowThreadProcessId(search.window, nullptr);
  const bool has_gui = GetGUIThreadInfo(thread, &gui) != 0;
  const auto layout = GetKeyboardLayout(thread);
  const HANDLE file = CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create result file");
  const std::string json = "{\"schema\":\"soui.v02.os-input/v1\",\"pid\":" + std::to_string(pid) +
      ",\"creationFiletime\":" + std::to_string(actual_creation) +
      ",\"hwnd\":" + std::to_string(reinterpret_cast<std::uintptr_t>(search.window)) +
      ",\"action\":\"" + std::string(action.begin(), action.end()) +
      "\",\"foregroundBefore\":" + (foreground ? "true" : "false") +
      ",\"before\":[" + std::to_string(before.left) + "," + std::to_string(before.top) + "," +
      std::to_string(before.right) + "," + std::to_string(before.bottom) + "]" +
      ",\"after\":[" + std::to_string(after.left) + "," + std::to_string(after.top) + "," +
      std::to_string(after.right) + "," + std::to_string(after.bottom) + "]" +
      ",\"keyboardLayout\":" + std::to_string(reinterpret_cast<std::uintptr_t>(layout)) +
      ",\"focusedHwnd\":" + std::to_string(has_gui ? reinterpret_cast<std::uintptr_t>(gui.hwndFocus) : 0) +
      ",\"imeCompositionVerified\":false,\"releaseReady\":false}\n";
  DWORD written = 0;
  const bool saved = WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) &&
                     written == json.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!saved) throw std::runtime_error("cannot persist input result");
  std::cout << json;
  return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

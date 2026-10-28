#include "miniblink_session.h"
#include "frontend_snapshot.h"
#include "callback_observation.h"
#include "miniblink_frontend_cleanup.h"
#include "../cursor_broker.h"
#include "../engine_admission.h"

#include <windowsx.h>
#include <imm.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace soui::sdk::detail {
namespace {
constexpr wchar_t inspector_class[] = L"SOUI.SDK.ControlledInspector.V1";
constexpr UINT paint_ready = WM_APP + 0x52B;
constexpr UINT_PTR inspector_tick = 1;
constexpr std::size_t max_pixels = 4096ULL * 4096ULL * 4ULL;
std::atomic<bool> inspector_owner_reserved{false};
host_result ok(const char* message = "") { return {true, {}, message}; }
host_result pending() { return {false, "E_DEVTOOLS_PENDING", "Controlled inspector ownership is pending; retain the Host and retry."}; }
host_result failed(const char* code, const char* message) { return {false, code, message}; }
bool valid_token(wkeCursorBrokerEndpoint value) { return value.low || value.high; }
bool same_identity(const wkeCursorBrokerIdentity& a, const wkeCursorBrokerIdentity& b) {
  return a.structSize == sizeof(a) && b.structSize == sizeof(b) && !a.reserved && !b.reserved &&
    a.viewGeneration == b.viewGeneration && a.documentGeneration == b.documentGeneration &&
    a.geometryGeneration == b.geometryGeneration;
}
const inspector_api* resolve_api(HMODULE module) noexcept {
  if (!module) return nullptr;
  auto get = reinterpret_cast<get_inspector_api>(GetProcAddress(module, "wkeStage0GetDevToolsApi"));
  if (!get) get = reinterpret_cast<get_inspector_api>(GetProcAddress(module, "_wkeStage0GetDevToolsApi"));
  const auto* api = get ? get(inspector_abi_version) : nullptr;
  return inspector_api_supported(api) ? api : nullptr;
}
unsigned keyboard_modifiers() noexcept {
  unsigned modifiers = 0;
  if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= inspector_shift;
  if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= inspector_control;
  if (GetKeyState(VK_MENU) & 0x8000) modifiers |= inspector_alt;
  if (GetKeyState(VK_CAPITAL) & 1) modifiers |= inspector_caps_lock;
  if (GetKeyState(VK_NUMLOCK) & 1) modifiers |= inspector_num_lock;
  if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) modifiers |= inspector_meta;
  return modifiers;
}
template<class Function> Function symbol(HMODULE module, const char* name) noexcept {
  return reinterpret_cast<Function>(GetProcAddress(module, name));
}
} // namespace

struct miniblink_devtools_session::state final : std::enable_shared_from_this<state> {
  enum class step { resources, create, registration, open, live, cleanup, closed };
  struct operation {
    std::atomic<bool> done{false};
    unsigned status{inspector_ok};
    bool waiting{false}, success{true};
    const char* code{""};
  };

  const DWORD ui_thread{GetCurrentThreadId()};
  miniblink_devtools_target target;
  const inspector_api* api{};
  std::function<bool()> target_current;
  std::function<void()> native_close;
  std::unique_ptr<frontend_snapshot_publication> publication;
  frontend_snapshot_owner* snapshot_owner{};
  std::unique_ptr<cursor_broker_endpoint> frontend_broker;
  std::shared_ptr<operation> task;
  step phase{step::resources};
  HWND window{};
  bool ui_closing{false}, close_notified{false};
  bool owns_runtime_inspector{true};
  bool endpoint_destroyed{false};
  std::atomic<bool> stopping{false}, fault{false}, frontend_ready{false};
  std::atomic<unsigned> paint_callbacks{0};
  std::atomic<bool> paint_posted{false};
  std::atomic<unsigned> native_status{0};
  std::atomic<unsigned> first_native_failure{0};
  std::uint64_t frontend_close_sequence{}; // Original kernel receipt; never replaced by a retry.
  std::atomic<int> caret_x{0}, caret_y{0};
  std::atomic<unsigned __int64> session{0};
  wkeCursorBrokerEndpoint frontend_endpoint{}; // Published by the actual Engine task completion.
  wkeCursorBrokerIdentity frontend_identity{};
  wkeWebView frontend_view{};
  std::string virtual_base, failure_code;
  FN_wkeLoadURL load_url{};
  FN_wkeOnPaintUpdated on_paint{};
  FN_wkeOnDocumentReady on_ready{};
  FN_wkeResize resize{};
  FN_wkeGetWidth get_width{};
  FN_wkeGetHeight get_height{};
  FN_wkeGetCaretRect get_caret{};
  std::mutex pixels_mutex;
  std::vector<unsigned char> pixels;
  int pixel_width{}, pixel_height{};

  state(miniblink_devtools_target value, const devtools::configuration& configuration,
      std::function<bool()> current, std::function<void()> close)
      : target(value), api(resolve_api(value.module)), target_current(std::move(current)),
        native_close(std::move(close)) {
    if (!api || !target.engine || !target.broker || !target_current)
      throw std::runtime_error("controlled inspector ABI unavailable");
    load_url = symbol<FN_wkeLoadURL>(target.module, "wkeLoadURL");
    on_paint = symbol<FN_wkeOnPaintUpdated>(target.module, "wkeOnPaintUpdated");
    on_ready = symbol<FN_wkeOnDocumentReady>(target.module, "wkeOnDocumentReady");
    resize = symbol<FN_wkeResize>(target.module, "wkeResize");
    get_width = symbol<FN_wkeGetWidth>(target.module, "wkeGetWidth");
    get_height = symbol<FN_wkeGetHeight>(target.module, "wkeGetHeight");
    get_caret = symbol<FN_wkeGetCaretRect>(target.module, "wkeGetCaretRect");
    if (!load_url || !on_paint || !on_ready || !resize || !get_width || !get_height)
      throw std::runtime_error("controlled inspector frontend exports unavailable");
    std::vector<wchar_t> module_path(32768);
    const auto length = GetModuleFileNameW(target.module, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (!length || length >= module_path.size()) throw std::runtime_error("loaded inspector kernel path unavailable");
    const std::filesystem::path kernel(std::wstring(module_path.data(), length));
    frontend_snapshot_inputs inputs{configuration.frontend_root,
      configuration.frontend_root / L"approved-manifest.json", configuration.frontend_manifest_sha256, 0};
    publication = std::make_unique<frontend_snapshot_publication>(std::move(inputs), kernel, configuration.kernel_sha256);
  }
  ~state() {
    // No background callback can outlive this retained state. A failed close
    // is not a disposal authorization; its adapter must remain owned.
    if (window || frontend_broker || valid_token(frontend_endpoint) || session.load() ||
        (task && !task->done.load(std::memory_order_acquire)) || (publication && !publication->ready()))
      std::terminate();
    if (snapshot_owner) snapshot_owner->release();
    if (owns_runtime_inspector) inspector_owner_reserved.store(false, std::memory_order_release);
  }
  bool owner() const noexcept { return GetCurrentThreadId() == ui_thread; }
  void remember_failure(const char* code) {
    if (failure_code.empty()) failure_code = code;
    fault.store(true, std::memory_order_release);
  }
  void begin_close() noexcept {
    if (!owner()) { fault.store(true, std::memory_order_release); return; }
    // The adapter's native callback closes Bridge admission before the kernel
    // close flag can cause an existing paused invocation to leave its stack.
    if (!close_notified) {
      close_notified = true;
      try { if (native_close) native_close(); }
      catch (...) { fault.store(true, std::memory_order_release); }
    }
    stopping.store(true, std::memory_order_release);
    const auto id = session.load(std::memory_order_acquire);
    if (id) {
      const auto status = api->requestClose(id);
      if (status != inspector_ok && status != inspector_closing)
        fault.store(true, std::memory_order_release);
    }
  }
  template<class Work> bool post(Work work) {
    if (task) return false;
    auto result = std::make_shared<operation>();
    const auto self = shared_from_this();
    const auto admitted = engine_admission::post(*target.engine, engine_lane::cleanup,
      [self, result, work = std::move(work)] {
        try { work(*self, *result); }
        catch (...) { result->success = false; result->code = "E_DEVTOOLS_ENGINE"; }
        self->native_status.store(result->status, std::memory_order_release);
        result->done.store(true, std::memory_order_release);
      });
    if (admitted != engine_admission_result::accepted) {
      if (admitted != engine_admission_result::full) remember_failure("E_DEVTOOLS_ENGINE_STOPPED");
      return false;
    }
    task = std::move(result);
    return true;
  }
  bool consume_task() {
    if (!task || !task->done.load(std::memory_order_acquire)) return false;
    if (!task->success) remember_failure(*task->code ? task->code : "E_DEVTOOLS_ENGINE");
    task.reset();
    return true;
  }
  static void WKE_CALL_TYPE ready(wkeWebView, void* context) noexcept {
    static_cast<state*>(context)->frontend_ready.store(true, std::memory_order_release);
  }
  static void WKE_CALL_TYPE paint(wkeWebView view, void* context, HDC source,
      int, int, int, int) noexcept {
    auto& self = *static_cast<state*>(context);
    self.paint_callbacks.fetch_add(1, std::memory_order_acq_rel);
    struct leave { state& value; ~leave() { value.paint_callbacks.fetch_sub(1, std::memory_order_acq_rel); } } guard{self};
    if (self.stopping.load(std::memory_order_acquire)) return;
    try {
      const int width = self.get_width(view), height = self.get_height(view);
      if (self.get_caret) {
        const auto caret = self.get_caret(view);
        self.caret_x.store(caret.x, std::memory_order_release);
        self.caret_y.store(caret.y + caret.h, std::memory_order_release);
      }
      if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        self.fault.store(true, std::memory_order_release); return;
      }
      const auto bytes = static_cast<std::size_t>(width) * height * 4;
      if (bytes > max_pixels) { self.fault.store(true, std::memory_order_release); return; }
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
      info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
      void* bits{};
      const auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
      const auto memory = CreateCompatibleDC(source);
      if (!bitmap || !memory || !bits) {
        if (bitmap) DeleteObject(bitmap); if (memory) DeleteDC(memory);
        self.fault.store(true, std::memory_order_release); return;
      }
      const auto previous = SelectObject(memory, bitmap);
      if (!previous || previous == HGDI_ERROR) {
        DeleteDC(memory); DeleteObject(bitmap); self.fault.store(true, std::memory_order_release); return;
      }
      struct gdi_owner {
        HDC dc; HBITMAP bitmap; HGDIOBJ previous;
        ~gdi_owner() { SelectObject(dc, previous); DeleteDC(dc); DeleteObject(bitmap); }
      } held{memory, bitmap, previous};
      if (!BitBlt(memory, 0, 0, width, height, source, 0, 0, SRCCOPY)) {
        self.fault.store(true, std::memory_order_release); return;
      }
      {
        std::lock_guard<std::mutex> lock(self.pixels_mutex);
        self.pixels.resize(bytes);
        std::memcpy(self.pixels.data(), bits, bytes);
        self.pixel_width = width; self.pixel_height = height;
      }
      if (!self.paint_posted.exchange(true, std::memory_order_acq_rel) &&
          !PostMessageW(self.window, paint_ready, 0, 0)) {
        self.paint_posted.store(false, std::memory_order_release);
        self.fault.store(true, std::memory_order_release);
      }
    } catch (...) { self.fault.store(true, std::memory_order_release); }
  }
  void input(UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    const auto id = session.load(std::memory_order_acquire);
    if (!id || stopping.load(std::memory_order_acquire)) return;
    inspector_input value{}; value.structSize = sizeof(value);
    value.x = GET_X_LPARAM(lparam); value.y = GET_Y_LPARAM(lparam);
    value.flags = static_cast<unsigned>(wparam);
    switch (message) {
      case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
      case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN:
      case WM_MBUTTONUP: case WM_MOUSEMOVE:
        value.kind = inspector_mouse; value.message = message; break;
      case WM_MOUSEWHEEL: {
        POINT point{value.x, value.y}; ScreenToClient(window, &point);
        value.x = point.x; value.y = point.y; value.kind = inspector_wheel;
        value.flags = GET_KEYSTATE_WPARAM(wparam); value.delta = GET_WHEEL_DELTA_WPARAM(wparam); break;
      }
      case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
      case WM_CHAR: case WM_SYSCHAR:
        value = make_inspector_keyboard_input(message, static_cast<unsigned>(wparam),
          static_cast<unsigned>(lparam), keyboard_modifiers());
        break;
      case WM_SETFOCUS: value.kind = inspector_focus; value.flags = 0; break;
      case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
          value.kind = inspector_resize; value.x = LOWORD(lparam); value.y = HIWORD(lparam); value.flags = 0;
        }
        break;
    }
    if (!value.kind) return;
    const auto status = api->input(id, &value);
    if (inspector_input_requires_failure(status)) {
      native_status.store(status, std::memory_order_release);
      try { remember_failure(status == inspector_busy ? "E_DEVTOOLS_INPUT_BUSY" :
          status == inspector_limit ? "E_DEVTOOLS_INPUT_LIMIT" : "E_DEVTOOLS_INPUT"); }
      catch (...) { fault.store(true, std::memory_order_release); }
      begin_close(); // Rejected discrete input is never reported as delivered.
    }
  }
  static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<state*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<state*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      self->window = window;
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    try {
      if (message == WM_CLOSE) { self->begin_close(); return 0; }
      if (message == WM_DPICHANGED && lparam) {
        const auto& rect = *reinterpret_cast<const RECT*>(lparam);
        (void)SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left,
          rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
      }
      if (message == WM_IME_STARTCOMPOSITION) {
        const auto context = ImmGetContext(window);
        if (context) {
          COMPOSITIONFORM form{}; form.dwStyle = CFS_POINT;
          form.ptCurrentPos = {self->caret_x.load(std::memory_order_acquire), self->caret_y.load(std::memory_order_acquire)};
          (void)ImmSetCompositionWindow(context, &form);
          CANDIDATEFORM candidate{}; candidate.dwStyle = CFS_CANDIDATEPOS;
          candidate.ptCurrentPos = form.ptCurrentPos; (void)ImmSetCandidateWindow(context, &candidate);
          ImmReleaseContext(window, context);
        }
        // The system composition window owns pre-edit text. Only committed
        // UTF-16 characters enter the fixed inspector input ABI below.
      }
      if (message == WM_IME_COMPOSITION && (lparam & GCS_RESULTSTR)) {
        const auto context = ImmGetContext(window);
        if (context) {
          wchar_t text[256]{};
          const LONG bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
          if (bytes < 0 || bytes % sizeof(wchar_t) || static_cast<std::size_t>(bytes) > sizeof(text)) {
            self->fault.store(true, std::memory_order_release);
          } else if (bytes && ImmGetCompositionStringW(context, GCS_RESULTSTR, text, bytes) == bytes) {
            for (LONG at = 0; at < bytes / static_cast<LONG>(sizeof(wchar_t)); ++at)
              self->input(WM_CHAR, static_cast<WPARAM>(text[at]), 0);
          }
          ImmReleaseContext(window, context);
        }
        return 0;
      }
      if (message == WM_TIMER && wparam == inspector_tick) { (void)self->advance(); return 0; }
      if (message == cursor_descriptor_message && self->frontend_broker) {
        (void)self->frontend_broker->process_descriptor(); return 0;
      }
      if (message == WM_SETCURSOR && self->frontend_broker && self->frontend_broker->select()) return TRUE;
      if (message == paint_ready) {
        self->paint_posted.store(false, std::memory_order_release);
        InvalidateRect(window, nullptr, FALSE); return 0;
      }
      if (message == WM_ERASEBKGND) return 1;
      if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        {
          std::lock_guard<std::mutex> lock(self->pixels_mutex);
          if (!self->pixels.empty()) {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = self->pixel_width; info.bmiHeader.biHeight = -self->pixel_height;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            if (!SetDIBitsToDevice(dc, 0, 0, self->pixel_width, self->pixel_height, 0, 0, 0,
                self->pixel_height, self->pixels.data(), &info, DIB_RGB_COLORS))
              self->fault.store(true, std::memory_order_release);
          }
        }
        EndPaint(window, &paint); return 0;
      }
      if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN) {
        SetFocus(window); SetCapture(window);
      }
      if ((message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP) && GetCapture() == window)
        ReleaseCapture();
      self->input(message, wparam, lparam);
    } catch (...) { self->fault.store(true, std::memory_order_release); self->begin_close(); }
    if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return DefWindowProcW(window, message, wparam, lparam);
  }
  bool create_window() {
    WNDCLASSW type{}; type.lpfnWndProc = window_proc;
    type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = inspector_class;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.style = CS_DBLCLKS;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    RECT work{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) return false;
    const int width = std::min(1100L, work.right - work.left);
    const int height = std::min(700L, work.bottom - work.top);
    if (width <= 0 || height <= 0) return false;
    wchar_t target_title[257]{};
    if (target.owner_window) (void)GetWindowTextW(target.owner_window, target_title, 257);
    std::wstring title = L"SOUI DevTools — ";
    title += *target_title ? target_title : L"target document ";
    if (!*target_title) title += std::to_wstring(target.receipt);
    if (!CreateWindowExW(0, inspector_class, title.c_str(), WS_OVERLAPPEDWINDOW,
        work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2,
        width, height, nullptr, nullptr, type.hInstance, this)) return false;
    // Every frontend is a fresh native endpoint. Its identity cannot alias the
    // target's receipt; the kernel additionally binds a random endpoint token.
    static std::atomic<std::uint64_t> generations{0};
    auto previous = generations.load(std::memory_order_acquire);
    do {
      if (previous == (std::numeric_limits<std::uint64_t>::max)()) return false;
    } while (!generations.compare_exchange_weak(previous, previous + 1,
      std::memory_order_acq_rel, std::memory_order_acquire));
    const auto generation = previous + 1;
    frontend_identity = {sizeof(frontend_identity), 0, generation, 1, 1};
    frontend_broker = cursor_broker_endpoint::create(window, frontend_identity);
    if (!frontend_broker || !SetTimer(window, inspector_tick, 20, nullptr)) return false;
    ShowWindow(window, SW_SHOW); return true;
  }
  void dispatch_owned_window() {
    if (!window) return;
    MSG message{};
    // Only this frontend HWND, never the application's FIFO or thread queue.
    // Reentrant timers see ui_closing and cannot advance this state machine.
    for (unsigned n = 0; n < 64 && PeekMessageW(&message, window, 0, 0, PM_REMOVE); ++n) {
      TranslateMessage(&message); DispatchMessageW(&message);
    }
  }
  host_result advance_cleanup() {
    begin_close();
    if (task) {
      if (!consume_task()) return pending();
    }
    if (publication) {
      if (!publication->ready()) return pending();
      snapshot_owner = publication->take();
      if (!snapshot_owner && *publication->error_code()) {
        native_status.store(publication->native_error(), std::memory_order_release);
        remember_failure(publication->error_code());
      }
      publication.reset();
    }
    if (session.load(std::memory_order_acquire) || valid_token(frontend_endpoint)) {
      const bool posted = post([](state& self, operation& result) {
        if (!self.target.engine->is_owner_thread()) {
          result.success = false; result.code = "E_THREAD_AFFINITY"; return;
        }
        const auto id = self.session.load(std::memory_order_acquire);
        if (id) {
          inspector_snapshot observed{}; observed.structSize = sizeof(observed);
          result.status = self.api->snapshot(id, &observed);
          if (result.status != inspector_ok || observed.structSize != sizeof(observed) ||
              observed.abiVersion != inspector_abi_version || observed.session != id) {
            result.success = false; result.code = "E_DEVTOOLS_RECEIPT"; return;
          }
          if (observed.pauseDepth || observed.dispatchDepth) { result.waiting = true; return; }
          result.status = self.api->detach(id);
          if (result.status == inspector_busy) { result.waiting = true; return; }
          if (result.status != inspector_ok) {
            result.success = false; result.code = "E_DEVTOOLS_DETACH"; return;
          }
          result.status = self.api->release(id);
          if (result.status == inspector_busy) { result.waiting = true; return; }
          if (result.status != inspector_ok) {
            result.success = false; result.code = "E_DEVTOOLS_RELEASE"; return;
          }
          self.session.store(0, std::memory_order_release);
        }
        if (!valid_token(self.frontend_endpoint)) return;
        const auto cleanup = advance_miniblink_frontend_cleanup(*self.target.broker,
          self.frontend_endpoint, self.frontend_close_sequence, self.first_native_failure);
        result.status = cleanup.status; result.success = cleanup.success;
        result.waiting = cleanup.waiting; result.code = cleanup.code;
        if (cleanup.destroyed) { self.frontend_view = nullptr; self.endpoint_destroyed = true; }
      });
      (void)posted;
      dispatch_owned_window(); return pending();
    }
    if (frontend_broker) {
      if (!frontend_broker->begin_close()) return failed("E_THREAD_AFFINITY", "Inspector native ownership differs.");
      if (frontend_broker->native_scopes() || !frontend_broker->finish_close()) {
        dispatch_owned_window(); return pending();
      }
      frontend_broker.reset();
    }
    if (paint_callbacks.load(std::memory_order_acquire)) return pending();
    if (snapshot_owner && snapshot_owner->references() != 1) return pending();
    if (window) {
      KillTimer(window, inspector_tick);
      if (GetCapture() == window) ReleaseCapture();
      if (!DestroyWindow(window)) return failed("E_DEVTOOLS_WINDOW_CLOSE", "The retained inspector window could not close.");
      window = nullptr;
    }
    { std::lock_guard<std::mutex> lock(pixels_mutex); std::vector<unsigned char>{}.swap(pixels); }
    if (snapshot_owner) { snapshot_owner->release(); snapshot_owner = nullptr; }
    phase = step::closed;
    if (owns_runtime_inspector) {
      owns_runtime_inspector = false;
      inspector_owner_reserved.store(false, std::memory_order_release);
    }
    return ok("Inspector detached and all frontend owners drained.");
  }
  host_result advance() {
    if (!owner()) return failed("E_THREAD_AFFINITY", "DevTools requires its original native UI owner.");
    if (ui_closing) return pending();
    ui_closing = true;
    struct leave { bool& flag; ~leave() { flag = false; } } guard{ui_closing};
    if (phase == step::closed) return ok();
    if (fault.load(std::memory_order_acquire) || stopping.load(std::memory_order_acquire)) {
      phase = step::cleanup; return advance_cleanup();
    }
    if (phase == step::resources) {
      if (!publication->ready()) return pending();
      snapshot_owner = publication->take();
      if (!snapshot_owner) {
        native_status.store(publication->native_error(), std::memory_order_release);
        remember_failure(*publication->error_code() ? publication->error_code() : "E_DEVTOOLS_RESOURCES");
        publication.reset(); phase = step::cleanup; return advance_cleanup();
      }
      publication.reset();
      if (!create_window()) {
        remember_failure("E_DEVTOOLS_WINDOW"); phase = step::cleanup; return advance_cleanup();
      }
      phase = step::create;
    }
    if (phase == step::create) {
      if (!task) {
        RECT rect{};
        if (!GetClientRect(window, &rect)) { remember_failure("E_DEVTOOLS_WINDOW"); return pending(); }
        const auto width = rect.right, height = rect.bottom;
        (void)post([width, height](state& self, operation& result) {
          if (!self.target_current() || self.stopping.load(std::memory_order_acquire)) {
            result.success = false; result.code = "E_DEVTOOLS_STALE_TARGET"; return;
          }
          wkeCursorBrokerEndpointConfig config{}; config.structSize = sizeof(config);
          config.ownerWindow = self.window; config.identity = self.frontend_identity;
          config.hooks = self.frontend_broker->hooks();
          auto snapshot = self.snapshot_owner->configuration();
          wkeCursorBrokerEndpointReceipt endpoint{}; endpoint.structSize = sizeof(endpoint);
          Stage0SnapshotReceipt resources{}; resources.structSize = sizeof(resources);
          result.status = self.api->createFrontendResources(self.target.realm, &config, &snapshot, &endpoint, &resources);
          // A token handed off on failure is still a real cleanup-only owner.
          if (valid_token(endpoint.endpoint)) {
            self.frontend_endpoint = endpoint.endpoint; self.frontend_view = endpoint.webView;
            if (!self.frontend_broker->bind(endpoint)) {
              result.success = false; result.code = "E_DEVTOOLS_FRONTEND_BIND"; return;
            }
          }
          if (result.status != inspector_ok || endpoint.structSize != sizeof(endpoint) ||
              !self.frontend_view || !same_identity(endpoint.identity, self.frontend_identity) ||
              endpoint.ownerThreadId != self.ui_thread || resources.structSize != sizeof(resources) || resources.abiVersion != 2) {
            result.success = false; result.code = "E_DEVTOOLS_FRONTEND_CREATE"; return;
          }
          const auto length = strnlen(resources.virtualBase, sizeof(resources.virtualBase));
          if (!length || length >= sizeof(resources.virtualBase)) {
            result.success = false; result.code = "E_DEVTOOLS_FRONTEND_ORIGIN"; return;
          }
          self.virtual_base.assign(resources.virtualBase, length);
          const std::string prefix = std::string("https://soui-inspector.invalid/") + snapshot.payloadId + "/";
          if (self.virtual_base.size() != prefix.size() + 33 || self.virtual_base.compare(0, prefix.size(), prefix) ||
              self.virtual_base.back() != '/') {
            result.success = false; result.code = "E_DEVTOOLS_FRONTEND_ORIGIN"; return;
          }
          for (auto at = prefix.size(); at + 1 < self.virtual_base.size(); ++at) {
            const char ch = self.virtual_base[at];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
              result.success = false; result.code = "E_DEVTOOLS_FRONTEND_ORIGIN"; return;
            }
          }
          self.on_paint(self.frontend_view, paint, &self);
          self.on_ready(self.frontend_view, ready, &self);
          self.resize(self.frontend_view, width, height);
        });
        return pending();
      }
      if (!consume_task()) return pending();
      if (fault.load(std::memory_order_acquire)) return advance_cleanup();
      phase = step::registration;
    }
    if (phase == step::registration) {
      const auto registration = frontend_broker->ole_registration_completion();
      if (!registration.observed) return pending();
      if (!registration.shape_valid || registration.cancelled || !registration.endpoint_matches ||
          !registration.identity_matches || !frontend_broker->input_allowed()) {
        remember_failure("E_DEVTOOLS_FRONTEND_OLE"); return advance_cleanup();
      }
      phase = step::open;
    }
    if (phase == step::open) {
      if (!task) {
        (void)post([](state& self, operation& result) {
          if (!self.target_current() || self.stopping.load(std::memory_order_acquire)) {
            result.success = false; result.code = "E_DEVTOOLS_STALE_TARGET"; return;
          }
          unsigned __int64 id{};
          result.status = self.api->open(self.target.endpoint, &self.target.identity,
            self.frontend_endpoint, &self.frontend_identity, &id);
          if (id) self.session.store(id, std::memory_order_release);
          if (result.status != inspector_ok || !id) {
            result.success = false; result.code = result.status == inspector_busy ? "E_DEVTOOLS_BUSY" : "E_DEVTOOLS_OPEN"; return;
          }
          if (self.stopping.load(std::memory_order_acquire) || !self.target_current()) {
            (void)self.api->requestClose(id); result.success = false; result.code = "E_DEVTOOLS_STALE_TARGET"; return;
          }
          self.load_url(self.frontend_view, (self.virtual_base + "front_end/inspector.html").c_str());
        });
        return pending();
      }
      if (!consume_task()) return pending();
      if (fault.load(std::memory_order_acquire)) return advance_cleanup();
      phase = step::live;
    }
    if (phase == step::live) {
      inspector_snapshot value{}; value.structSize = sizeof(value);
      const auto id = session.load(std::memory_order_acquire);
      const auto status = id ? api->snapshot(id, &value) : inspector_stale;
      if (status != inspector_ok || value.structSize != sizeof(value) || value.abiVersion != inspector_abi_version || value.session != id) {
        remember_failure("E_DEVTOOLS_RECEIPT"); return advance_cleanup();
      }
      if (value.failed) remember_failure("E_DEVTOOLS_KERNEL");
      if (value.closing || value.detached || fault.load(std::memory_order_acquire)) return advance_cleanup();
      return value.attached ? ok("Inspector attachment observed.") : pending();
    }
    return pending();
  }
};

bool miniblink_devtools_session::available(HMODULE module) noexcept { return resolve_api(module) != nullptr; }
miniblink_devtools_session::miniblink_devtools_session(miniblink_devtools_target target,
    const devtools::configuration& configuration, std::function<bool()> current, std::function<void()> close)
{
  bool empty = false;
  if (!inspector_owner_reserved.compare_exchange_strong(empty, true,
      std::memory_order_acq_rel, std::memory_order_acquire)) throw miniblink_devtools_busy{};
  try { state_ = std::make_shared<state>(target, configuration, std::move(current), std::move(close)); }
  catch (...) { inspector_owner_reserved.store(false, std::memory_order_release); throw; }
}
miniblink_devtools_session::~miniblink_devtools_session() = default;
host_result miniblink_devtools_session::advance() {
  const auto result = state_->advance();
  if (state_->phase == state::step::closed && state_->fault.load(std::memory_order_acquire))
    return failed(state_->failure_code.empty() ? "E_DEVTOOLS_FRONTEND" : state_->failure_code.c_str(),
      "The inspector failed; frontend owners drained, and the target still requires retirement.");
  return result;
}
host_result miniblink_devtools_session::revoke() {
  if (!state_->owner()) return failed("E_THREAD_AFFINITY", "DevTools requires its original native UI owner.");
  state_->begin_close(); return state_->advance();
}
bool miniblink_devtools_session::closing() const noexcept { return state_->stopping.load(std::memory_order_acquire); }
bool miniblink_devtools_session::drained() const noexcept { return state_->phase == state::step::closed; }
devtools::observation miniblink_devtools_session::observe() const {
  const auto& self = *state_;
  devtools::observation result;
  if (!self.owner()) { result.state = devtools::phase::failed; result.code = "E_THREAD_AFFINITY"; return result; }
  result.resources_retained = self.phase != state::step::closed;
  const auto initial_failure = self.first_native_failure.load(std::memory_order_acquire);
  result.native_error = static_cast<std::int32_t>(initial_failure ? initial_failure :
    self.native_status.load(std::memory_order_acquire));
  result.retirement_required = true; // This candidate accepts destroy_target only.
  result.state = self.phase == state::step::closed ? devtools::phase::drained :
    self.stopping.load(std::memory_order_acquire) ? devtools::phase::revoking : devtools::phase::opening;
  const auto id = self.session.load(std::memory_order_acquire);
  if (id) {
    inspector_snapshot value{}; value.structSize = sizeof(value);
    if (self.api->snapshot(id, &value) == inspector_ok && value.structSize == sizeof(value) &&
        value.abiVersion == inspector_abi_version && value.session == id) {
      result.attached = value.attached ? devtools::truth::yes : devtools::truth::no;
      result.paused = value.pauseDepth ? devtools::truth::yes : devtools::truth::no;
      devtools::callback_observation counts;
      counts.domain = devtools::callback_domain::miniblink_session;
      counts.sampled = true;
      counts.interrupt_pending = value.pendingCallbacks;
      counts.protocol_queued = value.queued;
      counts.resource_queued = value.resourceQueued;
      counts.dispatch_active = value.dispatchDepth;
      counts.resource_active = value.resourceActive;
      counts.frontend_paint_active = self.paint_callbacks.load(std::memory_order_acquire);
      if (!publish_callback_observation(result, counts)) {
        result.state = devtools::phase::failed;
        result.code = "E_DEVTOOLS_OBSERVATION";
        return result;
      }
      if (value.attached && !value.closing && !self.stopping.load(std::memory_order_acquire)) result.state = devtools::phase::attached;
      if (value.closing || value.detached) result.state = devtools::phase::revoking;
      if (value.failed) result.code = "E_DEVTOOLS_KERNEL";
    }
  } else if (self.phase == state::step::closed) {
    result.attached = result.paused = devtools::truth::no;
    result.callbacks.domain = devtools::callback_domain::miniblink_session;
    result.callbacks.sampled = true; // Original owner completed its strict drain.
  }
  // This HWND is SDK-owned; visibility is directly observable. No target HWND
  // scan or guessed backend window title is involved.
  result.visible = self.window && IsWindow(self.window) && IsWindowVisible(self.window) ?
    devtools::truth::yes : devtools::truth::no;
  if (self.fault.load(std::memory_order_acquire)) {
    result.code = self.failure_code.empty() ? "E_DEVTOOLS_FRONTEND" : self.failure_code;
    if (self.phase != state::step::closed) result.state = devtools::phase::failed;
  }
  return result;
}
} // namespace soui::sdk::detail

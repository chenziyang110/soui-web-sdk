#include <windows.h>
#include <gl/GL.h>
#include <cstdio>

namespace {
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  return DefWindowProcW(window, message, wparam, lparam);
}

bool cycle(HINSTANCE instance) {
  HWND window = CreateWindowExW(0, L"SouiWglLifecycleProbe", L"WGL probe",
                                WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
                                nullptr, nullptr, instance, nullptr);
  if (!window) return false;
  HDC dc = GetDC(window);
  PIXELFORMATDESCRIPTOR format{};
  format.nSize = sizeof(format);
  format.nVersion = 1;
  format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  format.iPixelType = PFD_TYPE_RGBA;
  format.cColorBits = 32;
  const int pixel_format = ChoosePixelFormat(dc, &format);
  const bool configured = pixel_format && SetPixelFormat(dc, pixel_format, &format);
  HGLRC context = configured ? wglCreateContext(dc) : nullptr;
  const bool bound = context && wglMakeCurrent(dc, context);
  if (bound) {
    glGetString(GL_VENDOR);
    using swap_interval_proc = BOOL (WINAPI*)(int);
    const auto swap_interval = reinterpret_cast<swap_interval_proc>(wglGetProcAddress("wglSwapIntervalEXT"));
    if (swap_interval) swap_interval(1);
    glClear(GL_COLOR_BUFFER_BIT);
    SwapBuffers(dc);
    wglMakeCurrent(nullptr, nullptr);
  }
  const bool deleted = context && wglDeleteContext(context);
  const bool released = ReleaseDC(window, dc) != 0;
  const bool destroyed = DestroyWindow(window) != 0;
  return configured && bound && deleted && released && destroyed;
}
}

int main() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW klass{};
  klass.cbSize = sizeof(klass);
  klass.hInstance = instance;
  klass.lpfnWndProc = window_proc;
  klass.lpszClassName = L"SouiWglLifecycleProbe";
  if (!RegisterClassExW(&klass)) return 2;
  for (int cycle_number = 1; cycle_number <= 33; ++cycle_number) {
    if (!cycle(instance)) return 3;
    Sleep(100);
    DWORD handles = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &handles)) return 4;
    std::printf("cycle=%d handles=%lu gdi=%lu user=%lu\n", cycle_number,
                handles, GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
                GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
  }
  return 0;
}

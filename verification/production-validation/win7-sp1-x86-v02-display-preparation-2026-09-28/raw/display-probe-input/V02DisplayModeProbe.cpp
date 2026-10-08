#include <windows.h>

#include <cstdio>
#include <cwchar>

namespace {
bool current_mode(DEVMODEW& mode) {
  mode = {};
  mode.dmSize = sizeof(mode);
  return EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) != FALSE;
}

void report(const char* phase, const DEVMODEW& mode) {
  std::printf("%s width=%lu height=%lu bpp=%lu refresh=%lu metrics=%dx%d\n", phase,
              mode.dmPelsWidth, mode.dmPelsHeight, mode.dmBitsPerPel,
              mode.dmDisplayFrequency, GetSystemMetrics(SM_CXSCREEN),
              GetSystemMetrics(SM_CYSCREEN));
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 2 || (std::wcscmp(argv[1], L"--list") != 0 &&
                    std::wcscmp(argv[1], L"--set-1024x768") != 0)) {
    std::fprintf(stderr, "usage: V02DisplayModeProbe --list|--set-1024x768\n");
    return 1;
  }

  DEVMODEW before{};
  if (!current_mode(before)) {
    std::fprintf(stderr, "cannot read current display mode: %lu\n", GetLastError());
    return 2;
  }
  report("before", before);

  DEVMODEW chosen{};
  bool exact_depth = false;
  unsigned candidates = 0;
  for (DWORD index = 0; index < 4096; ++index) {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, index, &mode)) break;
    if (mode.dmPelsWidth != 1024 || mode.dmPelsHeight != 768) continue;
    ++candidates;
    if (!chosen.dmSize || (!exact_depth && mode.dmBitsPerPel == before.dmBitsPerPel)) {
      chosen = mode;
      exact_depth = mode.dmBitsPerPel == before.dmBitsPerPel;
    }
  }
  std::printf("target=1024x768 candidates=%u matchingCurrentDepth=%d\n",
              candidates, exact_depth ? 1 : 0);
  if (std::wcscmp(argv[1], L"--list") == 0) return 0;
  if (candidates == 0) {
    std::fprintf(stderr, "1024x768 is unavailable\n");
    return 3;
  }

  chosen.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
  const LONG test = ChangeDisplaySettingsW(&chosen, CDS_TEST);
  std::printf("testResult=%ld\n", test);
  if (test != DISP_CHANGE_SUCCESSFUL) return 4;
  const LONG change = ChangeDisplaySettingsW(&chosen, 0);
  std::printf("changeResult=%ld\n", change);
  if (change != DISP_CHANGE_SUCCESSFUL) return 5;
  Sleep(700);
  DEVMODEW after{};
  if (!current_mode(after)) {
    std::fprintf(stderr, "cannot read changed display mode: %lu\n", GetLastError());
    return 6;
  }
  report("after", after);
  if (after.dmPelsWidth != 1024 || after.dmPelsHeight != 768 ||
      GetSystemMetrics(SM_CXSCREEN) != 1024 || GetSystemMetrics(SM_CYSCREEN) != 768) {
    std::fprintf(stderr, "display dimensions differ after change\n");
    return 7;
  }
  return 0;
}

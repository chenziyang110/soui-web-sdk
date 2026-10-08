#include "../../demos/workbench/app/work_area_placement.h"

#include <cstdlib>
#include <iostream>

void expect(bool condition, const char* name) {
  if (condition) return;
  std::cerr << "Window placement failed: " << name << '\n';
  std::exit(1);
}

int main() {
  using soui::demo::fit_initial_window;

  // The Win7 taskbar reduces a 1024x768 display to a 1024x729 work area.
  const auto win7 = fit_initial_window({0, 0, 1024, 729}, 850, 640);
  expect(win7 && win7->left == 87 && win7->top == 25 &&
         win7->width == 850 && win7->height == 640, "Win7 work area");
  expect(win7->left + win7->width + 80 < 1024 &&
         win7->top + win7->height + 60 < 729,
         "Win7 resize drag stays above taskbar");

  const auto oversized = fit_initial_window({0, 0, 1024, 729}, 1100, 720);
  expect(oversized && oversized->left == 8 && oversized->top == 8 &&
         oversized->width == 932 && oversized->height == 657, "oversized default");
  expect(oversized->left + oversized->width + 80 < 1024 &&
         oversized->top + oversized->height + 60 < 729,
         "default resize drag stays inside work area");

  // The original 800x600 Win7 guest leaves 560 pixels above its taskbar.
  const auto small = fit_initial_window({0, 0, 800, 560}, 850, 640);
  expect(small && small->left == 8 && small->top == 8 &&
         small->width == 708 && small->height == 488, "small Win7 work area");
  expect(small->left + small->width + 80 < 800 &&
         small->top + small->height + 60 < 560,
         "small Win7 resize drag stays above taskbar");

  const auto too_small = fit_initial_window({0, 0, 700, 500}, 850, 640);
  expect(too_small && too_small->left == 0 && too_small->top == 0 &&
         too_small->width == 700 && too_small->height == 500,
         "work area too small for a usable resize margin");

  const auto secondary = fit_initial_window({-1920, -100, 0, 980}, 1100, 720);
  expect(secondary && secondary->left == -1510 && secondary->top == 80 &&
         secondary->width == 1100 && secondary->height == 720, "negative monitor origin");

  const auto left_taskbar = fit_initial_window({48, 0, 1024, 768}, 850, 640);
  expect(left_taskbar && left_taskbar->left == 90 && left_taskbar->top == 64 &&
         left_taskbar->width == 850 && left_taskbar->height == 640,
         "left taskbar work area");

  expect(!fit_initial_window({0, 0, 0, 729}, 850, 640), "invalid work area");
  expect(!fit_initial_window({0, 0, 1024, 729}, 0, 640), "invalid request");
}

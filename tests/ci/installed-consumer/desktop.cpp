#include <soui/desktop/app.h>

// Compile/link-only on every platform. This target is never a CTest test and
// the workflow must not launch it. Do not initialize a backend or enter its loop.
int main() {
  soui::desktop::options settings;
  settings.visible = false;
  soui::desktop::application application(settings);
  const auto capabilities = application.get_capabilities();
  return capabilities.backend.empty() ? 1 : 0;
}

#include "startup_dialog.h"
#define SOUI_STARTUP_TRACE_NAMESPACE soui_app_startup_trace
#include "startup_trace.h"

#import <AppKit/AppKit.h>

#if !__has_feature(objc_arc)
#error "The template startup dialog requires Objective-C ARC (-fobjc-arc)."
#endif

namespace soui_app {
namespace {
namespace trace = soui_app_startup_trace;

bool valid_text(const std::string& text) noexcept {
  if (text.empty() || text.size() > 4096) return false;
  for (const unsigned char byte : text) {
    if (byte != '\n' && (byte < 0x20 || byte > 0x7e)) return false;
  }
  return true;
}

}  // namespace

bool show_startup_error(const std::string& sanitized_text) noexcept {
  if (!valid_text(sanitized_text)) return false;
  try {
    @try {
      @autoreleasepool {
        if (![NSThread isMainThread]) return false;
        // Initialize AppKit only if it is available. Do not require activation
        // policy changes, force foreground activation, or restart NSApp's loop.
        trace::mark(trace::stage::dialog_appkit_enter);
        const bool appkit_loaded = NSApplicationLoad();
        trace::mark(trace::stage::dialog_appkit_return);
        if (!appkit_loaded || !NSApp) return false;
        // A prohibited application or missing screen cannot host a usable modal
        // alert. Leave those conditions unchanged and retain the stderr path.
        if (NSApp.activationPolicy == NSApplicationActivationPolicyProhibited) return false;
        trace::mark(trace::stage::dialog_screens_enter);
        const auto screens = [[NSScreen screens] count];
        trace::mark(trace::stage::dialog_screens_return);
        if (screens == 0) return false;
        NSString* message = [[NSString alloc] initWithBytes:sanitized_text.data()
            length:sanitized_text.size() encoding:NSASCIIStringEncoding];
        if (!message) return false;
        NSAlert* alert = [[NSAlert alloc] init];
        if (!alert) return false;
        alert.alertStyle = NSAlertStyleCritical;
        alert.messageText = @"Application startup failed";
        alert.informativeText = message;
        [alert addButtonWithTitle:@"OK"];
        trace::mark(trace::stage::dialog_modal_enter);
        const auto response = [alert runModal];
        trace::mark(trace::stage::dialog_modal_return);
        return response == NSAlertFirstButtonReturn;
      }
    } @catch (id exception) {
      (void)exception;
      return false;
    }
  } catch (...) {
    return false;
  }
}

}  // namespace soui_app

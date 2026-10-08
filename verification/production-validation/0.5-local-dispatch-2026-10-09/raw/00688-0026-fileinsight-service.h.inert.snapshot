#pragma once

#include <string_view>

#include <soui/runtime/bridge/bridge_transport.h>
#include <soui/runtime/services/filesystem-readonly/directory_scanner.h>

namespace soui::modules::fileinsight {

// A feature-owned service facade. Traversal, permission checks, bounded result
// storage and task lifecycle remain in the shared directory_scanner; this
// facade gives generated modules a stable, product-level method namespace.
class FileinsightService {
 public:
  static void register_methods(soui::runtime::capability_registry& registry,
                               soui::runtime::directory_scanner& scanner);
};

}  // namespace soui::modules::fileinsight

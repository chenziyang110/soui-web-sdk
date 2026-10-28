#pragma once

#include <soui/desktop/app.h>
#include <cstddef>

namespace soui::desktop::detail {
constexpr std::size_t maximum_asset_bytes = 256 * 1024;

struct asset_result {
  result status;
  std::string mime;
  std::string base64;
  std::size_t size{0};
};

// The root must be an application-owned deployment directory that remains
// trusted and immutable while the application runs. Canonical containment does
// not protect against a malicious concurrent replacement of filesystem entries.
asset_result read_asset(const std::filesystem::path& root,
                        std::string_view relative_path) noexcept;
}  // namespace soui::desktop::detail

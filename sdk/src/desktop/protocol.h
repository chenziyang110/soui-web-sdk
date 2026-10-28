#pragma once

#include <soui/desktop/app.h>
#include <cstdint>
#include <json.hpp>

namespace soui::desktop::detail {
constexpr std::size_t maximum_request_bytes = 65536;
constexpr std::size_t maximum_response_bytes = 1048576;
constexpr std::uint64_t maximum_request_id = 9007199254740991ULL;

struct document {
  std::filesystem::path path;
  std::string uri;
};
result resolve_document(const std::filesystem::path& root,
                        const std::filesystem::path& relative,
                        document& selected);
bool same_document_uri(std::string_view actual, std::string_view selected) noexcept;
bool valid_name(std::string_view name) noexcept;
nlohmann::json parse_bounded_json(std::string_view text, std::size_t maximum);
std::string make_preload(std::string_view navigation_token);
}  // namespace soui::desktop::detail

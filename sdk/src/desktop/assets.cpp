#include "assets.h"
#include "../runtime/platform/strict_utf8.h"
#include <fstream>

namespace soui::desktop::detail {
namespace {
asset_result failure(std::string code, std::string message) {
  return {result::failure(std::move(code), std::move(message)), {}, {}, 0};
}

bool valid_path(std::string_view path) noexcept {
  if (path.empty() || path.size() > 4096 ||
      !soui::runtime::detail::valid_utf8(path)) return false;
  for (std::size_t index = 0; index < path.size(); ++index) {
    const auto value = static_cast<unsigned char>(path[index]);
    if (value < 32 || value == 127 || value == '\\' || value == ':' ||
        value == '%' || value == '?' || value == '#') return false;
    // UTF-8 encodings of the remaining Unicode control characters (C1).
    if (value == 0xc2U && index + 1 < path.size() &&
        static_cast<unsigned char>(path[index + 1]) <= 0x9fU) return false;
  }
  std::size_t begin = 0;
  while (begin < path.size()) {
    const auto end = path.find('/', begin);
    const auto part = path.substr(begin, end == std::string_view::npos
                                          ? path.size() - begin : end - begin);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string_view::npos) return true;
    begin = end + 1;
  }
  return false;  // A trailing slash leaves an empty final segment.
}

bool contained_by(const std::filesystem::path& root,
                  const std::filesystem::path& candidate) {
  auto selected = candidate.begin();
  for (const auto& part : root) {
    if (selected == candidate.end() || part != *selected) return false;
    ++selected;
  }
  return selected != candidate.end();
}

const char* mime_type(const std::filesystem::path& path) {
  auto extension = path.extension().u8string();
  for (char& value : extension)
    if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
  struct entry {
    std::string_view extension;
    const char* mime;
  };
  constexpr entry types[] = {
    {".html", "text/html"}, {".htm", "text/html"}, {".css", "text/css"},
    {".js", "text/javascript"}, {".mjs", "text/javascript"},
    {".json", "application/json"}, {".txt", "text/plain"}, {".csv", "text/csv"},
    {".xml", "application/xml"}, {".svg", "image/svg+xml"},
    {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
    {".gif", "image/gif"}, {".webp", "image/webp"}, {".ico", "image/x-icon"},
    {".wasm", "application/wasm"}, {".pdf", "application/pdf"},
    {".woff", "font/woff"}, {".woff2", "font/woff2"},
    {".ttf", "font/ttf"}, {".otf", "font/otf"},
    {".mp3", "audio/mpeg"}, {".wav", "audio/wav"}, {".ogg", "audio/ogg"},
    {".mp4", "video/mp4"}, {".webm", "video/webm"}
  };
  for (const auto& type : types)
    if (extension == type.extension) return type.mime;
  return "application/octet-stream";
}

std::string encode_base64(std::string_view bytes) {
  constexpr char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve(((bytes.size() + 2) / 3) * 4);
  for (std::size_t index = 0; index < bytes.size(); index += 3) {
    const auto remaining = bytes.size() - index;
    const auto first = static_cast<unsigned char>(bytes[index]);
    const auto second = remaining > 1
      ? static_cast<unsigned char>(bytes[index + 1]) : 0U;
    const auto third = remaining > 2
      ? static_cast<unsigned char>(bytes[index + 2]) : 0U;
    encoded += alphabet[first >> 2U];
    encoded += alphabet[((first & 3U) << 4U) | (second >> 4U)];
    encoded += remaining > 1 ? alphabet[((second & 15U) << 2U) | (third >> 6U)] : '=';
    encoded += remaining > 2 ? alphabet[third & 63U] : '=';
  }
  return encoded;
}
}  // namespace

asset_result read_asset(const std::filesystem::path& root,
                        std::string_view relative_path) noexcept {
  try {
    if (!valid_path(relative_path))
      return failure("invalid_asset_path", "Asset path must be a bounded UTF-8 root-relative file path.");
    const auto relative = std::filesystem::u8path(relative_path.begin(), relative_path.end());
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
      return failure("invalid_asset_path", "Asset path must be relative to the resource root.");

    std::error_code error;
    const auto canonical_root = std::filesystem::canonical(root, error);
    if (error || !std::filesystem::is_directory(canonical_root, error) || error)
      return failure("invalid_root", "Resource root must be an existing local directory.");
    const auto candidate = std::filesystem::canonical(canonical_root / relative, error);
    if (error)
      return failure("asset_not_found", "Asset must be an existing regular file.");
    if (!contained_by(canonical_root, candidate))
      return failure("invalid_asset_path", "Asset resolves outside the resource root.");
    if (!std::filesystem::is_regular_file(candidate, error) || error)
      return failure("asset_not_found", "Asset must be an existing regular file.");
    const auto advertised_size = std::filesystem::file_size(candidate, error);
    if (error)
      return failure("asset_read_failed", "Unable to inspect the local asset.");
    if (advertised_size > maximum_asset_bytes)
      return failure("asset_too_large", "Asset exceeds the 256 KiB limit.");

    // Read one extra byte rather than trusting the size observed before open.
    // This bounds allocation and detects growth beyond the supported payload.
    std::ifstream stream(candidate, std::ios::binary);
    if (!stream)
      return failure("asset_read_failed", "Unable to open the local asset.");
    std::string bytes(maximum_asset_bytes + 1, '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = static_cast<std::size_t>(stream.gcount());
    if (count > maximum_asset_bytes)
      return failure("asset_too_large", "Asset exceeds the 256 KiB limit.");
    if (stream.bad() || (stream.fail() && !stream.eof()))
      return failure("asset_read_failed", "Unable to read the local asset.");
    bytes.resize(count);
    return {result::success(), mime_type(candidate), encode_base64(bytes), count};
  } catch (...) {
    // Even an allocation failure must not escape the native Bridge boundary.
    asset_result rejected;
    try {
      rejected.status = result::failure("asset_read_failed", "Unable to read the local asset.");
    } catch (...) {}
    return rejected;
  }
}
}  // namespace soui::desktop::detail

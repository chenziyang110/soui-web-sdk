#pragma once
// Ported from experiments/devtools-stage0/resources/offline_snapshot.h.
// Private SDK resource reader; no experiment or installed-path dependency.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace soui::sdk::detail::inspector_resources {

inline constexpr std::uint64_t max_manifest_bytes = 512 * 1024;
inline constexpr std::uint64_t max_kernel_image_bytes = 256ULL * 1024 * 1024;
inline constexpr std::size_t max_files = 1024;
inline constexpr std::uint64_t max_file_bytes = 1024 * 1024;
inline constexpr std::uint64_t max_total_bytes = 16 * 1024 * 1024;
inline constexpr std::size_t max_path_bytes = 256;
inline constexpr std::size_t max_segment_bytes = 128;
inline constexpr std::size_t max_path_depth = 8;
inline constexpr std::size_t max_directories = 2048;
inline constexpr std::size_t max_root_characters = 2048;
inline constexpr std::string_view virtual_origin = "soui-inspector://stage0/";

using digest = std::array<std::uint8_t, 32>;
using immutable_bytes = std::shared_ptr<const std::vector<std::uint8_t>>;

enum class failure {
    invalid_root, invalid_path, unknown_resource, manifest_duplicate, budget,
    open, reparse, identity, size, digest_mismatch, read, changed, crypto
};

class resource_error final : public std::runtime_error {
public:
    resource_error(failure code, const char* reason, unsigned long native_error = 0);
    [[nodiscard]] failure code() const noexcept { return code_; }
    [[nodiscard]] unsigned long native_error() const noexcept { return native_error_; }
private:
    failure code_;
    unsigned long native_error_;
};

struct manifest_entry final {
    std::string path;
    std::uint64_t bytes{};
    digest sha256{};
};

struct snapshot_identity final {
    std::uint32_t volume{};
    std::uint64_t root_file_index{};
    std::uint64_t bytes{};
    std::size_t files{};
};

class resource_snapshot final {
public:
    [[nodiscard]] static resource_snapshot load(const std::filesystem::path& approved_root,
        const std::vector<manifest_entry>& approved_manifest);
    [[nodiscard]] immutable_bytes read_virtual(std::string_view fixed_origin_url) const;
    [[nodiscard]] const snapshot_identity& identity() const noexcept { return identity_; }
private:
    using resources = std::map<std::string, immutable_bytes, std::less<>>;
    resource_snapshot(resources contents, snapshot_identity identity);
    resources contents_;
    snapshot_identity identity_;
};

// Reads only the fixed package manifest, using the same plain-object, local-root,
// deny-write/delete, EOF and stable-identity checks as member loading. The digest
// identifies the captured body; it is not an external authenticity endorsement.
struct package_manifest final {
    immutable_bytes body;
    digest sha256{};
};
[[nodiscard]] package_manifest read_package_manifest(const std::filesystem::path& approved_root);

// Verifies only the native caller's captured loaded-kernel file against its
// independent package pin. Streams at most max_kernel_image_bytes; never loads
// or executes the image and never returns file contents to a caller.
void verify_kernel_image(const std::filesystem::path& loaded_kernel, std::string_view expected_sha256);

[[nodiscard]] digest sha256(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] digest parse_digest(std::string_view lowercase_hex);
[[nodiscard]] std::string digest_hex(const digest& value);
[[nodiscard]] const char* failure_name(failure value) noexcept;

} // namespace soui::sdk::detail::inspector_resources

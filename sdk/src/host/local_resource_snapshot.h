#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace soui::sdk::detail {

inline constexpr std::uint64_t local_resource_file_bytes = 16ULL * 1024 * 1024;
inline constexpr std::uint64_t local_resource_total_bytes = 128ULL * 1024 * 1024;
inline constexpr std::size_t local_resource_files = 4096;
inline constexpr std::size_t local_resource_directories = 4096;
inline constexpr std::size_t local_resource_path_bytes = 1024;
inline constexpr std::size_t local_resource_segment_bytes = 128;
inline constexpr std::size_t local_resource_path_depth = 32;
inline constexpr std::size_t local_resource_name_bytes = 8 * 1024 * 1024;
inline constexpr std::size_t local_resource_process_snapshots = 32;
inline constexpr std::uint64_t local_resource_process_bytes = 512ULL * 1024 * 1024;

enum class local_resource_failure {
    invalid_root, invalid_path, budget, open, reparse, identity, read,
    changed, missing_entry, crypto
};

class local_resource_error final : public std::runtime_error {
public:
    local_resource_error(local_resource_failure, const char* reason,
        unsigned long native_error = 0);
    [[nodiscard]] local_resource_failure code() const noexcept { return code_; }
    [[nodiscard]] unsigned long native_error() const noexcept { return native_error_; }
private:
    local_resource_failure code_;
    unsigned long native_error_;
};

struct local_resource final {
    std::vector<std::uint8_t> bytes;
    // Business-resource ABI 1 MIME enumeration. Unknown extensions are octet
    // stream (0); this is separate from the inspector snapshot policy.
    std::uint32_t mime{};
};

struct local_resource_identity final {
    std::uint32_t volume{};
    std::uint64_t root_file_index{};
    std::string sha256;
};

// A captured, closed resource tree. Preparation performs bounded file I/O and
// belongs on the Host publication worker, never a native response callback.
// Once returned, lookup and every getter are memory-only and immutable.
class local_resource_snapshot final {
public:
    using resources = std::map<std::string, local_resource, std::less<>>;
    // An empty entry selects react.html when present, otherwise index.html.
    // Explicit entries are restricted to those same two fixed documents.
    [[nodiscard]] static std::shared_ptr<const local_resource_snapshot> prepare(
        const std::filesystem::path& root, std::string entry_key);
    ~local_resource_snapshot();
    local_resource_snapshot(const local_resource_snapshot&) = delete;
    local_resource_snapshot& operator=(const local_resource_snapshot&) = delete;
    local_resource_snapshot(local_resource_snapshot&&) = delete;
    local_resource_snapshot& operator=(local_resource_snapshot&&) = delete;
    [[nodiscard]] const local_resource* lookup(std::string_view relative_key) const noexcept;
    [[nodiscard]] const std::string& entry_key() const noexcept { return entry_key_; }
    [[nodiscard]] std::size_t file_count() const noexcept { return contents_.size(); }
    [[nodiscard]] std::uint64_t total_bytes() const noexcept { return total_bytes_; }
    [[nodiscard]] const local_resource_identity& identity() const noexcept { return identity_; }
    [[nodiscard]] const resources& entries() const noexcept { return contents_; }
    [[nodiscard]] static bool valid_key(std::string_view) noexcept;

private:
    // Private behavior fixture exercises the same process-credit primitive
    // without allocating 512 MiB of file bodies. No public SDK seam is added.
    friend struct local_resource_snapshot_budget_test;
    struct budget_lease final {
        budget_lease();
        ~budget_lease();
        budget_lease(const budget_lease&) = delete;
        budget_lease& operator=(const budget_lease&) = delete;
        budget_lease(budget_lease&&) = delete;
        budget_lease& operator=(budget_lease&&) = delete;
        void reserve(std::uint64_t);
        std::uint64_t bytes{};
        bool bytes_reserved{};
    };
    local_resource_snapshot(std::unique_ptr<budget_lease>, resources, std::string,
        std::uint64_t, local_resource_identity);
    // Members are destroyed in reverse order. Keep this before contents_ so
    // all actual response bodies are freed before process credits are returned.
    std::unique_ptr<budget_lease> budget_;
    resources contents_;
    std::string entry_key_;
    std::uint64_t total_bytes_{};
    local_resource_identity identity_;
};

[[nodiscard]] const char* local_resource_failure_name(local_resource_failure) noexcept;

} // namespace soui::sdk::detail

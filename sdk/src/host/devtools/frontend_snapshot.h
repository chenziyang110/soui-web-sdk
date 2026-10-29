#pragma once
// Ported from experiments/devtools-stage0/miniblink/resource_snapshot_shim.h.
// Private SDK snapshot owner and asynchronous package publication.

#include "snapshot_callback_abi.h"
#include "offline_snapshot.h"

#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <windows.h>

namespace soui::sdk::detail {

struct frontend_snapshot_inputs final {
    std::filesystem::path root, manifest;
    std::string manifest_sha256;
    unsigned int manifest_bytes{};
};

class frontend_snapshot_owner final {
public:
    // These loaders perform file I/O and belong on the publication worker.
    [[nodiscard]] static frontend_snapshot_owner* load(const frontend_snapshot_inputs&);
    [[nodiscard]] Stage0SnapshotConfig configuration() noexcept;
    [[nodiscard]] unsigned int references() const noexcept { return references_.load(); }
    void release() noexcept;
    frontend_snapshot_owner(const frontend_snapshot_owner&) = delete;
    frontend_snapshot_owner& operator=(const frontend_snapshot_owner&) = delete;
    frontend_snapshot_owner(frontend_snapshot_owner&&) = delete;
    frontend_snapshot_owner& operator=(frontend_snapshot_owner&&) = delete;

private:
    frontend_snapshot_owner() = default;
    struct entry final { inspector_resources::immutable_bytes bytes; unsigned int mime; };
    std::map<std::string, entry, std::less<>> entries_;
    std::string payload_id_;
    unsigned int total_bytes_{};
    std::atomic<unsigned int> references_{1};
    [[nodiscard]] static frontend_snapshot_owner* load_body(const std::filesystem::path& root,
        const inspector_resources::immutable_bytes& body, const std::string& payload_id);
    static void __cdecl retain_callback(void*) noexcept;
    static void __cdecl release_callback(void*) noexcept;
    static unsigned int __cdecl lookup_callback(void*, const char*, unsigned int,
        const unsigned char**, unsigned int*, unsigned int*) noexcept;
};

// Retain the publication and its native handle until ready() observes real
// worker exit. UI/engine callbacks only poll; they never join or perform I/O.
class frontend_snapshot_publication final {
public:
    // Product path: verify both independent native configuration pins on this
    // worker before publishing any frontend resources. A zero manifest_bytes
    // means the fixed manifest's actual bounded extent is read by the worker.
    frontend_snapshot_publication(frontend_snapshot_inputs, std::filesystem::path loaded_kernel,
        std::string kernel_sha256);
    ~frontend_snapshot_publication();
    frontend_snapshot_publication(const frontend_snapshot_publication&) = delete;
    frontend_snapshot_publication& operator=(const frontend_snapshot_publication&) = delete;
    frontend_snapshot_publication(frontend_snapshot_publication&&) = delete;
    frontend_snapshot_publication& operator=(frontend_snapshot_publication&&) = delete;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] frontend_snapshot_owner* take() noexcept;
    // Fixed, non-sensitive diagnostics become readable only after real worker
    // exit; pending returns E_DEVTOOLS_PENDING and no native error.
    [[nodiscard]] const char* error_code() const noexcept;
    [[nodiscard]] unsigned long native_error() const noexcept;

private:
    frontend_snapshot_inputs inputs_;
    std::filesystem::path loaded_kernel_;
    std::string kernel_sha256_;
    HANDLE worker_{};
    frontend_snapshot_owner* result_{};
    const char* error_code_{"E_DEVTOOLS_PENDING"};
    unsigned long native_error_{};
    std::atomic<bool> done_{false};
    void start();
    static unsigned __stdcall run(void*) noexcept;
};

} // namespace soui::sdk::detail

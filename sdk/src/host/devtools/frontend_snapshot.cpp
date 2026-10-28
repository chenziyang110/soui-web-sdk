#include "frontend_snapshot.h"
// Ported from experiments/devtools-stage0/miniblink/resource_snapshot_shim.cpp.

#include <json.hpp>
#include <process.h>
#include <cstring>
#include <exception>
#include <memory>
#include <string_view>
#include <utility>

namespace soui::sdk::detail {
namespace rr = inspector_resources;
namespace {
void require(bool value) {
    if (!value) throw std::runtime_error("snapshot input rejected");
}
bool approved_manifest_name(const std::filesystem::path& path) {
    auto name = path.filename().wstring();
    for (auto& ch : name)
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
    return name == L"approved-manifest.json";
}
bool same_native_root(const std::filesystem::path& first, const std::filesystem::path& second) {
    auto a = first.native(), b = second.native();
    for (auto& ch : a) if (ch == L'/') ch = L'\\';
    for (auto& ch : b) if (ch == L'/') ch = L'\\';
    while (a.size() > 3 && a.back() == L'\\') a.pop_back();
    while (b.size() > 3 && b.back() == L'\\') b.pop_back();
    return a.size() <= rr::max_root_characters && a.size() == b.size()
        && CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
            b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
unsigned int mime(const std::string& path) noexcept {
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return 0;
    const std::string_view ext(path.data() + dot + 1, path.size() - dot - 1);
    if (ext == "html") return 1;
    if (ext == "js") return 2;
    if (ext == "css") return 3;
    if (ext == "json" || ext == "map") return 4;
    if (ext == "png") return 5;
    if (ext == "svg") return 6;
    if (ext == "gif") return 7;
    if (ext == "jpeg" || ext == "jpg") return 8;
    if (ext == "woff") return 9;
    if (ext == "woff2") return 10;
    if (ext == "txt") return 11;
    if (ext == "wasm") return 12;
    return 0;
}
} // namespace

frontend_snapshot_owner* frontend_snapshot_owner::load(const frontend_snapshot_inputs& inputs) {
    require(inputs.root.is_absolute() && inputs.manifest.is_absolute()
        && approved_manifest_name(inputs.manifest)
        && same_native_root(inputs.root, inputs.manifest.parent_path())
        && inputs.manifest_bytes <= rr::max_manifest_bytes);
    const auto approved = rr::parse_digest(inputs.manifest_sha256);
    const auto manifest = rr::read_package_manifest(inputs.root);
    if (manifest.sha256 != approved)
        throw rr::resource_error(rr::failure::digest_mismatch, "manifest-digest-mismatch");
    if (inputs.manifest_bytes && manifest.body->size() != inputs.manifest_bytes)
        throw rr::resource_error(rr::failure::size, "manifest-size-mismatch");
    return load_body(inputs.root, manifest.body, inputs.manifest_sha256);
}

frontend_snapshot_owner* frontend_snapshot_owner::load_body(const std::filesystem::path& root,
    const rr::immutable_bytes& body, const std::string& payload_id) {
    std::size_t nodes = 0;
    const auto json = nlohmann::json::parse(body->begin(), body->end(),
        [&](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
            require(depth <= 32 && ++nodes <= 32768);
            return true;
        });
    require(json.is_array() && !json.empty() && json.size() <= rr::max_files);
    std::vector<rr::manifest_entry> manifest;
    for (const auto& item : json) {
        require(item.is_object() && item.size() == 3 && item.at("path").is_string()
            && item.at("bytes").is_number_unsigned() && item.at("sha256").is_string());
        manifest.push_back({item.at("path").get<std::string>(), item.at("bytes").get<std::uint64_t>(),
            rr::parse_digest(item.at("sha256").get<std::string>())});
    }
    const auto snapshot = rr::resource_snapshot::load(root, manifest);
    auto owner = std::unique_ptr<frontend_snapshot_owner>(new frontend_snapshot_owner());
    owner->payload_id_ = payload_id;
    owner->total_bytes_ = static_cast<unsigned int>(snapshot.identity().bytes);
    for (const auto& item : manifest)
        owner->entries_.emplace(item.path, entry{
            snapshot.read_virtual(std::string(rr::virtual_origin) + item.path), mime(item.path)});
    require(owner->entries_.count("front_end/inspector.html") == 1);
    return owner.release();
}

void __cdecl frontend_snapshot_owner::retain_callback(void* value) noexcept {
    ++static_cast<frontend_snapshot_owner*>(value)->references_;
}
void __cdecl frontend_snapshot_owner::release_callback(void* value) noexcept {
    static_cast<frontend_snapshot_owner*>(value)->release();
}
void frontend_snapshot_owner::release() noexcept {
    if (references_.fetch_sub(1) == 1) delete this;
}
unsigned int __cdecl frontend_snapshot_owner::lookup_callback(void* value, const char* path,
    unsigned int length, const unsigned char** bytes, unsigned int* size, unsigned int* type) noexcept {
    if (!bytes || !size || !type) return 0;
    *bytes = nullptr;
    *size = 0;
    *type = 0;
    if (!value || !path || !length || length > rr::max_path_bytes) return 0;
    // Exact heterogeneous lookup: no allocation, file I/O, callbacks or waits.
    const auto& owner = *static_cast<frontend_snapshot_owner*>(value);
    const auto found = owner.entries_.find(std::string_view(path, length));
    if (found == owner.entries_.end()) return 0;
    *bytes = found->second.bytes->data();
    *size = static_cast<unsigned int>(found->second.bytes->size());
    *type = found->second.mime;
    return 1;
}
Stage0SnapshotConfig frontend_snapshot_owner::configuration() noexcept {
    Stage0SnapshotConfig config{};
    config.structSize = sizeof(config);
    config.abiVersion = 2;
    config.fileCount = static_cast<unsigned int>(entries_.size());
    config.totalBytes = total_bytes_;
    std::memcpy(config.payloadId, payload_id_.c_str(), payload_id_.size() + 1);
    config.callbacks = {sizeof(config.callbacks), 2, this,
        retain_callback, release_callback, lookup_callback};
    return config;
}

frontend_snapshot_publication::frontend_snapshot_publication(frontend_snapshot_inputs inputs,
    std::filesystem::path loaded_kernel, std::string kernel_sha256)
    : inputs_(std::move(inputs)), loaded_kernel_(std::move(loaded_kernel)),
      kernel_sha256_(std::move(kernel_sha256)) {
    start();
}
void frontend_snapshot_publication::start() {
    worker_ = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, run, this, 0, nullptr));
    require(worker_ != nullptr);
}
frontend_snapshot_publication::~frontend_snapshot_publication() {
    // The session retains a pending publication on deadline/failure.
    if (worker_ && !ready()) std::terminate();
    if (worker_) CloseHandle(worker_);
    if (result_) result_->release();
}
unsigned __stdcall frontend_snapshot_publication::run(void* value) noexcept {
    auto& self = *static_cast<frontend_snapshot_publication*>(value);
    bool kernel_verified = false;
    try {
        rr::verify_kernel_image(self.loaded_kernel_, self.kernel_sha256_);
        kernel_verified = true;
        self.result_ = frontend_snapshot_owner::load(self.inputs_);
        self.error_code_ = self.result_ ? "" : "E_DEVTOOLS_FRONTEND_RESOURCE";
    } catch (const rr::resource_error& error) {
        self.error_code_ = kernel_verified ? "E_DEVTOOLS_FRONTEND_RESOURCE" : "E_DEVTOOLS_KERNEL_IDENTITY";
        self.native_error_ = error.native_error();
    } catch (const nlohmann::json::exception&) {
        self.error_code_ = kernel_verified ? "E_DEVTOOLS_FRONTEND_MANIFEST" : "E_DEVTOOLS_KERNEL_IDENTITY";
    } catch (const std::runtime_error&) {
        self.error_code_ = kernel_verified ? "E_DEVTOOLS_FRONTEND_MANIFEST" : "E_DEVTOOLS_KERNEL_IDENTITY";
    } catch (...) {
        self.error_code_ = kernel_verified ? "E_DEVTOOLS_FRONTEND_RESOURCE" : "E_DEVTOOLS_KERNEL_IDENTITY";
    }
    self.done_.store(true, std::memory_order_release);
    return self.result_ ? 0 : 1;
}
bool frontend_snapshot_publication::ready() const noexcept {
    return done_.load(std::memory_order_acquire) && worker_
        && WaitForSingleObject(worker_, 0) == WAIT_OBJECT_0;
}
frontend_snapshot_owner* frontend_snapshot_publication::take() noexcept {
    if (!ready()) return nullptr;
    DWORD exit = 1;
    if (!GetExitCodeThread(worker_, &exit) || exit != 0) return nullptr;
    auto* result = result_;
    result_ = nullptr;
    return result;
}
const char* frontend_snapshot_publication::error_code() const noexcept {
    return ready() ? error_code_ : "E_DEVTOOLS_PENDING";
}
unsigned long frontend_snapshot_publication::native_error() const noexcept {
    return ready() ? native_error_ : 0;
}

} // namespace soui::sdk::detail

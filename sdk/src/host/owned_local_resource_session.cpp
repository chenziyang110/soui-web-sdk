#include "owned_local_resource_session.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <process.h>
#endif

namespace soui::sdk::detail {
namespace {
host_result good() { return {true, {}, {}}; }
host_result pending() {
  return {false, "E_LOCAL_RESOURCE_PENDING", "The original local-resource owner has not retired."};
}
host_result error(const char* code) {
  return {false, code, "The explicitly selected local HTTPS resource mode failed; its original owners are retained until retirement."};
}
const char* snapshot_error_code(local_resource_failure reason) noexcept {
  switch (reason) {
    case local_resource_failure::invalid_root: return "E_LOCAL_RESOURCE_ROOT";
    case local_resource_failure::invalid_path: return "E_LOCAL_RESOURCE_PATH";
    case local_resource_failure::budget: return "E_LOCAL_RESOURCE_LIMIT";
    case local_resource_failure::open: return "E_LOCAL_RESOURCE_OPEN";
    case local_resource_failure::reparse: return "E_LOCAL_RESOURCE_REPARSE";
    case local_resource_failure::identity: return "E_LOCAL_RESOURCE_IDENTITY";
    case local_resource_failure::read: return "E_LOCAL_RESOURCE_READ";
    case local_resource_failure::changed: return "E_LOCAL_RESOURCE_CHANGED";
    case local_resource_failure::missing_entry: return "E_LOCAL_RESOURCE_ENTRY";
    case local_resource_failure::crypto: return "E_LOCAL_RESOURCE_CRYPTO";
  }
  return "E_LOCAL_RESOURCE_SNAPSHOT";
}
bool hex32(std::string_view text) noexcept {
  return text.size() == 32 && std::all_of(text.begin(), text.end(), [](unsigned char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
  });
}
struct call_guard final {
  std::atomic<bool>& active;
  explicit call_guard(std::atomic<bool>& value) noexcept : active(value) { active.store(true, std::memory_order_release); }
  ~call_guard() { active.store(false, std::memory_order_release); }
};
template<std::size_t Size>
bool bounded_string(const char (&value)[Size], std::string& text) {
  const auto end = static_cast<const char*>(std::memchr(value, 0, Size));
  if (!end) return false;
  text.assign(value, end);
  return true;
}

struct callback_owner final {
  std::atomic<unsigned> references{1};
  std::shared_ptr<const local_resource_snapshot> snapshot;
  explicit callback_owner(std::shared_ptr<const local_resource_snapshot> value) : snapshot(std::move(value)) {}
  static void SOUI_LOCAL_RESOURCE_CALL retain(void* value) noexcept {
    auto& owner = *static_cast<callback_owner*>(value);
    if (owner.references.fetch_add(1, std::memory_order_relaxed) == (std::numeric_limits<unsigned>::max)())
      std::terminate();
  }
  static void SOUI_LOCAL_RESOURCE_CALL release(void* value) noexcept {
    auto* owner = static_cast<callback_owner*>(value);
    const auto before = owner->references.fetch_sub(1, std::memory_order_acq_rel);
    if (!before) std::terminate();
    if (before == 1) delete owner;
  }
  static std::uint32_t SOUI_LOCAL_RESOURCE_CALL lookup(void* value, const char* path,
      std::uint32_t length, const unsigned char** data, std::uint32_t* size, std::uint32_t* mime) noexcept {
    if (!value || !path || !data || !size || !mime || length > local_resource_path_bytes) return 2;
    *data = nullptr; *size = 0; *mime = SouiLocalOctet;
    const std::string_view key(path, length);
    if (!local_resource_snapshot::valid_key(key)) return 2;
    const auto& owner = *static_cast<callback_owner*>(value);
    const auto* found = owner.snapshot->lookup(key);
    if (!found) return 0;
    if (found->bytes.size() > local_resource_file_bytes || found->mime > SouiLocalMp3) return 2;
    *data = found->bytes.empty() ? nullptr : found->bytes.data();
    *size = static_cast<std::uint32_t>(found->bytes.size());
    *mime = found->mime;
    return 1;
  }
};
} // namespace

struct owned_local_resource_session::publication final {
  std::filesystem::path root;
  std::string entry;
  validator validate;
  std::shared_ptr<const local_resource_snapshot> snapshot;
  host_result result{false, "E_LOCAL_RESOURCE_PENDING", "The local-resource publication is pending."};
  unsigned long native_error{};
  const char* failure_code{};
  std::atomic<bool> done{false};
#ifdef _WIN32
  HANDLE worker{};
  static unsigned __stdcall run(void* raw) noexcept {
    auto& value = *static_cast<publication*>(raw);
    try {
      value.snapshot = local_resource_snapshot::prepare(value.root, std::move(value.entry));
      value.result = value.validate(*value.snapshot);
      if (!value.result.ok) value.snapshot.reset();
    } catch (const local_resource_error& cause) {
      value.native_error = cause.native_error();
      value.failure_code = snapshot_error_code(cause.code());
      value.result = error(value.failure_code);
      value.result.message = std::string("Local resource preparation rejected: ") +
          local_resource_failure_name(cause.code()) + "; native=" + std::to_string(cause.native_error());
    } catch (...) { value.result = error("E_LOCAL_RESOURCE_SNAPSHOT"); }
    value.done.store(true, std::memory_order_release);
    return value.result.ok ? 0U : 1U;
  }
#endif
  bool ready() const noexcept {
#ifdef _WIN32
    return worker && done.load(std::memory_order_acquire) && WaitForSingleObject(worker, 0) == WAIT_OBJECT_0;
#else
    return true;
#endif
  }
  ~publication() {
#ifdef _WIN32
    if (worker && !ready()) std::terminate();
    if (worker) CloseHandle(worker);
#endif
  }
};

owned_local_resource_session::owned_local_resource_session() = default;
owned_local_resource_session::~owned_local_resource_session() {
  if (!physically_retired()) std::terminate();
}
void owned_local_resource_session::fail(const char* code) noexcept {
  const char* empty = nullptr;
  (void)failure_code_.compare_exchange_strong(empty, code, std::memory_order_acq_rel);
  revoke_admission();
}
void owned_local_resource_session::revoke_admission() noexcept {
  close_admission(true);
}
void owned_local_resource_session::close_admission(bool invalidate_transition) noexcept {
  admission_.store(false, std::memory_order_release);
  load_armed_.store(false, std::memory_order_release);
  context_receipt_.store(0, std::memory_order_release);
  try {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    if (invalidate_transition) {
      pending_bind_epoch_ = 0;
      if (admission_epoch_ != (std::numeric_limits<std::uint64_t>::max)()) ++admission_epoch_;
      else {
        const char* empty = nullptr;
        (void)failure_code_.compare_exchange_strong(empty, "E_LOCAL_RESOURCE_GENERATION", std::memory_order_acq_rel);
      }
    }
    // Repeat under the commit lock: a concurrent bind cannot reopen UI-revoked admission.
    admission_.store(false, std::memory_order_release);
    load_armed_.store(false, std::memory_order_release);
    context_receipt_.store(0, std::memory_order_release);
  } catch (...) {
    const char* empty = nullptr;
    (void)failure_code_.compare_exchange_strong(empty, "E_LOCAL_RESOURCE_IDENTITY_LOCK", std::memory_order_acq_rel);
  }
}
bool owned_local_resource_session::begin_reload() noexcept {
  try {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    if (failed() || pending_bind_epoch_ || !native_receipt_.load(std::memory_order_acquire) ||
        admission_epoch_ == (std::numeric_limits<std::uint64_t>::max)()) return false;
    ++admission_epoch_;
    pending_bind_epoch_ = admission_epoch_;
    admission_.store(false, std::memory_order_release);
    load_armed_.store(false, std::memory_order_release);
    context_receipt_.store(0, std::memory_order_release);
    return true;
  } catch (...) { return false; }
}
bool owned_local_resource_session::publication_retired() const noexcept {
  try {
    std::lock_guard<std::mutex> lock(publication_mutex_);
    return !publication_ || publication_->ready();
  } catch (...) { return false; }
}
bool owned_local_resource_session::release_unbound_publication() {
  if (native_call_active_.load(std::memory_order_acquire) || !payload_retired()) return false;
  std::lock_guard<std::mutex> lock(publication_mutex_);
  if (publication_ && !publication_->ready()) return false;
  snapshot_.reset(); publication_.reset();
  return true;
}
bool owned_local_resource_session::preparation_expired() const noexcept {
#ifdef _WIN32
  return startup_deadline_ms_ && GetTickCount64() >= startup_deadline_ms_;
#else
  return true;
#endif
}
host_result owned_local_resource_session::prepare(const std::filesystem::path& root,
    std::string entry, validator validate, std::uint64_t startup_deadline_ms) {
#ifdef _WIN32
  if (publication_) return error("E_LOCAL_RESOURCE_ALREADY_PREPARING");
  startup_deadline_ms_ = startup_deadline_ms;
  std::array<unsigned char, 16> random{};
  if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    fail("E_LOCAL_RESOURCE_RANDOM"); return terminal_result();
  }
  static constexpr char alphabet[] = "0123456789abcdef";
  host_nonce_.reserve(32);
  for (auto byte : random) { host_nonce_ += alphabet[byte >> 4]; host_nonce_ += alphabet[byte & 15]; }
  publication_ = std::make_unique<publication>();
  publication_->root = root; // Original native spelling; never canonicalize away a reparse ancestor.
  publication_->entry = std::move(entry);
  publication_->validate = std::move(validate);
  publication_->worker = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, publication::run, publication_.get(), 0, nullptr));
  if (!publication_->worker) {
    publication_.reset(); fail("E_LOCAL_RESOURCE_WORKER"); return terminal_result();
  }
  const auto now = GetTickCount64();
  const auto remaining = startup_deadline_ms_ > now ? startup_deadline_ms_ - now : 0;
  const auto wait = WaitForSingleObject(publication_->worker,
      static_cast<DWORD>((std::min<std::uint64_t>)(remaining, 5000)));
  if (wait != WAIT_OBJECT_0 || !publication_->ready() || preparation_expired()) {
    fail(wait == WAIT_FAILED ? "E_LOCAL_RESOURCE_WORKER" : "E_LOCAL_RESOURCE_STARTUP_TIMEOUT");
    return terminal_result(); // Keep the real worker, its inputs and eventual snapshot until actual exit.
  }
  if (!publication_->result.ok || !publication_->snapshot) {
    if (publication_->native_error) {
      std::int32_t empty = 0;
      (void)native_error_.compare_exchange_strong(empty,
          static_cast<std::int32_t>(publication_->native_error), std::memory_order_acq_rel);
    }
    const auto& code = publication_->result.code;
    fail(publication_->failure_code ? publication_->failure_code :
        code == "E_UNTRUSTED_FRAME" ? "E_UNTRUSTED_FRAME" :
        code == "E_REMOTE_RESOURCE" ? "E_REMOTE_RESOURCE" : "E_LOCAL_RESOURCE_SNAPSHOT");
    return terminal_result();
  }
  {
    std::lock_guard<std::mutex> lock(publication_mutex_);
    snapshot_ = publication_->snapshot;
  }
  return good();
#else
  (void)root; (void)entry; (void)validate; (void)startup_deadline_ms;
  fail("E_LOCAL_RESOURCE_UNSUPPORTED"); return terminal_result();
#endif
}

host_result owned_local_resource_session::bind_on_engine(void* module, void* view,
    SouiLocalResourceEndpoint endpoint, std::uint64_t view_generation, std::uint64_t broker_document_generation) {
#ifdef _WIN32
  if (native_call_active_) return pending();
  if (failed()) return terminal_result();
  if (!publication_retired() || !snapshot_ || !retired_.load(std::memory_order_acquire) ||
      final_close_ || !module || !view || (!endpoint.low && !endpoint.high) || !view_generation || !broker_document_generation) {
    fail("E_LOCAL_RESOURCE_BIND_IDENTITY"); return terminal_result();
  }
  std::uint64_t expected_admission = 0;
  {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    expected_admission = pending_bind_epoch_;
    if (!expected_admission || expected_admission != admission_epoch_)
      return error("E_LOCAL_RESOURCE_CANCELLED");
  }
  const SouiLocalResourceApi* api = nullptr;
#ifdef SOUI_LOCAL_RESOURCE_HOST_TEST_ONLY
  api = &test_api_;
#else
  const auto get = reinterpret_cast<SouiGetLocalResourceApi>(GetProcAddress(static_cast<HMODULE>(module), "wkeGetLocalResourceApi"));
  api = get ? get(SOUI_LOCAL_RESOURCE_ABI) : nullptr;
#endif
  if (!api || api->structSize != sizeof(SouiLocalResourceApi) || api->abiVersion != SOUI_LOCAL_RESOURCE_ABI ||
      (api->capabilities & 7U) != 7U || !api->bind || !api->beginClose || !api->queryDrain) {
    fail("E_LOCAL_RESOURCE_UNSUPPORTED"); return terminal_result();
  }
  api_ = *api;
  endpoint_ = endpoint;
  SouiLocalResourceConfig config{};
  config.structSize = sizeof(config); config.abiVersion = SOUI_LOCAL_RESOURCE_ABI;
  config.fileCount = static_cast<std::uint32_t>(snapshot_->file_count());
  config.totalBytes = static_cast<std::uint32_t>(snapshot_->total_bytes());
  if (!hex32(host_nonce_) || snapshot_->entry_key().size() >= sizeof(config.entryRelative) ||
      snapshot_->identity().sha256.size() != 64) {
    fail("E_LOCAL_RESOURCE_SNAPSHOT"); return terminal_result();
  }
  std::memcpy(config.hostNonce, host_nonce_.data(), host_nonce_.size());
  std::memcpy(config.entryRelative, snapshot_->entry_key().data(), snapshot_->entry_key().size());
  std::memcpy(config.payloadId, snapshot_->identity().sha256.data(), snapshot_->identity().sha256.size());
  auto* owner = new callback_owner(snapshot_);
  config.callbacks = {sizeof(SouiLocalResourceCallbacks), SOUI_LOCAL_RESOURCE_ABI, owner,
      callback_owner::retain, callback_owner::release, callback_owner::lookup};
  SouiLocalResourceReceipt observed{};
  observed.structSize = sizeof(observed);
  close_requested_ = false; close_sequence_ = 0;
  std::uint32_t status = SouiLocalFailed;
  {
    call_guard guarded(native_call_active_);
    status = api_.bind(endpoint_, view_generation, broker_document_generation, &config, &observed);
  }
  if (status != SouiLocalOk && status != SouiLocalPending) {
    std::int32_t empty = 0;
    (void)native_error_.compare_exchange_strong(empty, static_cast<std::int32_t>(status), std::memory_order_acq_rel);
  }
  callback_owner::release(owner); // Native publication retains its own independent callback-owner reference.
  if (observed.documentReceipt) {
    receipt_ = observed; // Even a failed bind can have published an owner requiring actual retirement.
    retired_.store(false, std::memory_order_release);
    native_receipt_.store(observed.documentReceipt, std::memory_order_release);
  }
  std::string base, entry;
  const std::string prefix = "https://" + host_nonce_ + ".soui-local.invalid/";
  const bool shape = observed.structSize == sizeof(observed) && observed.abiVersion == SOUI_LOCAL_RESOURCE_ABI &&
      observed.documentReceipt && observed.viewGeneration == view_generation &&
      observed.boundBrokerDocumentGeneration == broker_document_generation &&
      bounded_string(observed.virtualBase, base) && bounded_string(observed.entryUrl, entry) &&
      base.size() == prefix.size() + 33 && base.compare(0, prefix.size(), prefix) == 0 &&
      base.back() == '/' && hex32(std::string_view(base).substr(prefix.size(), 32)) &&
      entry == base + snapshot_->entry_key();
  if (status != SouiLocalOk || !shape) {
    fail(status == SouiLocalLimit ? "E_LOCAL_RESOURCE_LIMIT" : "E_LOCAL_RESOURCE_BIND"); return terminal_result();
  }
  context_receipt_.store(0, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    if (failed() || admission_epoch_ != expected_admission || pending_bind_epoch_ != expected_admission)
      return error("E_LOCAL_RESOURCE_CANCELLED");
    view_ = view; entry_ = std::move(entry);
    pending_bind_epoch_ = 0;
    load_armed_.store(true, std::memory_order_release);
    admission_.store(true, std::memory_order_release);
  }
  return good();
#else
  (void)module; (void)view; (void)endpoint; (void)view_generation; (void)broker_document_generation;
  fail("E_LOCAL_RESOURCE_UNSUPPORTED"); return terminal_result();
#endif
}

bool owned_local_resource_session::accept_drain(std::uint32_t status,
    const SouiLocalResourceDrain& value, bool closing) {
  if (status != SouiLocalOk && status != SouiLocalPending) {
    std::int32_t empty = 0;
    (void)native_error_.compare_exchange_strong(empty, static_cast<std::int32_t>(status), std::memory_order_acq_rel);
  }
  if (status != SouiLocalOk && status != SouiLocalPending && status != SouiLocalFailed) {
    fail("E_LOCAL_RESOURCE_DRAIN"); return false;
  }
  if (value.structSize != sizeof(value) || value.abiVersion != SOUI_LOCAL_RESOURCE_ABI ||
      value.documentReceipt != receipt_.documentReceipt || value.viewGeneration != receipt_.viewGeneration ||
      value.failed > 1 || value.payloadHeld > 1 || value.activeOperations > SOUI_LOCAL_RESOURCE_MAX_OPERATIONS ||
      value.callbackBorrows > SOUI_LOCAL_RESOURCE_MAX_CALLBACK_BORROWS ||
      value.queuedOperations > SOUI_LOCAL_RESOURCE_MAX_QUEUED_OPERATIONS ||
      value.borrowedBytes > SOUI_LOCAL_RESOURCE_MAX_BORROWED_BYTES ||
      value.phase < SouiLocalReady || value.phase > SouiLocalPayloadRetired ||
      (closing && (value.phase == SouiLocalReady || !value.closeSequence)) ||
      (close_sequence_ && value.closeSequence != close_sequence_)) {
    fail("E_LOCAL_RESOURCE_DRAIN_RECEIPT"); return false;
  }
  if (value.closeSequence) close_sequence_ = value.closeSequence;
  if (status == SouiLocalFailed || value.failed) {
    if (value.failed) {
      std::int32_t empty = 0;
      (void)native_error_.compare_exchange_strong(empty, SouiLocalFailed, std::memory_order_acq_rel);
    }
    fail("E_LOCAL_RESOURCE_NATIVE");
  }
  if (value.phase == SouiLocalPayloadRetired) {
    if (value.activeOperations || value.callbackBorrows || value.queuedOperations || value.payloadHeld || value.borrowedBytes) {
      fail("E_LOCAL_RESOURCE_DRAIN_RECEIPT"); return false;
    }
    // A refused outer native call can expose an already retired state while
    // still returning Pending. Only an accepted terminal query completes the
    // caller's physical fence; never upgrade a Pending status to success.
    if (status != SouiLocalPending) retired_.store(true, std::memory_order_release);
    close_admission(false); // A successful reload retirement retains its one admitted successor permit.
  }
  return true;
}
host_result owned_local_resource_session::retire_on_engine(bool final_close) {
  final_close_ = final_close_ || final_close;
  close_admission(final_close_);
  if (native_call_active_.load(std::memory_order_acquire)) return pending();
  if (!publication_retired()) return pending();
  if (!retired_.load(std::memory_order_acquire)) {
    if (native_call_active_) return pending();
    SouiLocalResourceDrain value{}; value.structSize = sizeof(value);
    std::uint32_t status = SouiLocalFailed;
    {
      call_guard guarded(native_call_active_);
      status = close_requested_ ? api_.queryDrain(endpoint_, receipt_.documentReceipt, &value)
          : api_.beginClose(endpoint_, receipt_.documentReceipt, &value);
    }
    if (!close_requested_ && status == SouiLocalPending && value.phase == SouiLocalReady && !value.closeSequence) {
      if (!accept_drain(status, value, false)) return terminal_result();
      return pending(); // Native did not accept close yet: retry beginClose, not queryDrain.
    }
    if (!accept_drain(status, value, true)) return terminal_result();
    close_requested_ = true;
  }
  if (!retired_.load(std::memory_order_acquire)) return pending();
  if (final_close_) {
    std::lock_guard<std::mutex> lock(publication_mutex_);
    snapshot_.reset();
    publication_.reset(); // The retained thread has signalled; this never detaches an active worker.
  }
  return terminal_result();
}
void owned_local_resource_session::observe_on_engine() {
  if (native_call_active_ || retired_.load(std::memory_order_acquire) || !receipt_.documentReceipt) return;
  SouiLocalResourceDrain value{}; value.structSize = sizeof(value);
  std::uint32_t status = SouiLocalFailed;
  {
    call_guard guarded(native_call_active_);
    status = api_.queryDrain(endpoint_, receipt_.documentReceipt, &value);
  }
  (void)accept_drain(status, value, close_requested_);
}
bool owned_local_resource_session::payload_retired() const noexcept {
  return retired_.load(std::memory_order_acquire);
}
bool owned_local_resource_session::physically_retired() const noexcept {
  if (native_call_active_.load(std::memory_order_acquire) || !payload_retired()) return false;
  try {
    std::lock_guard<std::mutex> lock(publication_mutex_);
    return !publication_ && !snapshot_ && !native_call_active_.load(std::memory_order_acquire) && payload_retired();
  } catch (...) { return false; }
}
host_result owned_local_resource_session::terminal_result() const {
  const auto* code = failure_code_.load(std::memory_order_acquire);
  return code ? error(code) : good();
}
std::string owned_local_resource_session::entry_url() const {
  std::lock_guard<std::mutex> lock(identity_mutex_);
  return entry_;
}
bool owned_local_resource_session::allow_navigation(void* view, std::string_view url) noexcept {
  if (!trusted_entry(view, url)) return false;
  if (url.find('#') != std::string_view::npos)
    return !load_armed_.load(std::memory_order_acquire) &&
        accepts_context(view, context_receipt_.load(std::memory_order_acquire));
  // The original native load has one admission. A second same-URL load must
  // use the Host's reload path, which retires the old owner and rotates nonce.
  bool armed = true;
  return load_armed_.compare_exchange_strong(armed, false, std::memory_order_acq_rel) &&
      admission_.load(std::memory_order_acquire);
}
bool owned_local_resource_session::trusted_entry(void* view, std::string_view url) const noexcept {
  if (!admission_.load(std::memory_order_acquire) || failed() || !native_document_receipt()) return false;
  url = url.substr(0, url.find('#'));
  try {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    return view && view == view_ && !entry_.empty() && url == entry_ &&
        admission_.load(std::memory_order_acquire) && !failed();
  } catch (...) { return false; }
}
std::uint64_t owned_local_resource_session::native_document_receipt() const noexcept {
  return native_receipt_.load(std::memory_order_acquire);
}
bool owned_local_resource_session::acknowledge_context(void* view, std::string_view url,
    std::uint64_t native_receipt, std::uint64_t context_receipt) noexcept {
  if (!context_receipt || (context_receipt & 1U) || native_document_receipt() != native_receipt ||
      load_armed_.load(std::memory_order_acquire) || !trusted_entry(view, url)) return false;
  std::uint64_t empty = 0;
  return context_receipt_.compare_exchange_strong(empty, context_receipt, std::memory_order_acq_rel) &&
      native_document_receipt() == native_receipt && trusted_entry(view, url);
}
bool owned_local_resource_session::accepts_context(void* view, std::uint64_t context_receipt) const noexcept {
  if (!accepts_context(context_receipt)) return false;
  try {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    return view && view == view_ && admission_.load(std::memory_order_acquire) && !failed() &&
        context_receipt_.load(std::memory_order_acquire) == context_receipt;
  } catch (...) { return false; }
}
bool owned_local_resource_session::accepts_context(std::uint64_t context_receipt) const noexcept {
  if (!context_receipt || context_receipt_.load(std::memory_order_acquire) != context_receipt ||
      !admission_.load(std::memory_order_acquire) || failed() || payload_retired()) return false;
  try {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    return view_ && admission_.load(std::memory_order_acquire) && !failed() &&
        context_receipt_.load(std::memory_order_acquire) == context_receipt;
  } catch (...) { return false; }
}
#ifdef SOUI_LOCAL_RESOURCE_HOST_TEST_ONLY
void* owned_local_resource_session::test_publication_handle() const noexcept {
  try {
    std::lock_guard<std::mutex> lock(publication_mutex_);
#ifdef _WIN32
    return publication_ ? publication_->worker : nullptr;
#else
    return nullptr;
#endif
  } catch (...) { return nullptr; }
}
#endif

} // namespace soui::sdk::detail

#include "../../sdk/src/host/owned_local_resource_session.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
using soui::sdk::detail::local_resource_snapshot;
using soui::sdk::detail::owned_local_resource_session;

void require(bool value, const char* message) {
  if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
struct resources final {
  std::filesystem::path root;
  resources() {
    wchar_t temp[MAX_PATH + 1]{};
    const auto length = GetTempPathW(MAX_PATH, temp);
    require(length && length < MAX_PATH, "A plain local test directory is required");
    static unsigned sequence = 0;
    root = std::filesystem::path(temp) / (L"soui-local-host-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence));
    require(std::filesystem::create_directory(root), "The test root must be fresh");
    std::ofstream(root / "index.html", std::ios::binary) << "<!doctype html><title>local</title>";
    std::ofstream(root / "network.json", std::ios::binary) << "{\"answer\":42}";
    std::ofstream(root / "empty.txt", std::ios::binary);
  }
  ~resources() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
};

struct native_model final {
  static native_model* current;
  SouiLocalResourceCallbacks callbacks{};
  SouiLocalResourceReceipt receipt{};
  unsigned bind_calls{}, close_calls{}, query_calls{}, releases{};
  std::uint32_t phase{SouiLocalReady}, active{}, borrows{}, queued{};
  std::uint64_t borrowed{}, close_sequence{};
  std::uint64_t denied{};
  bool failed{}, fail_bind_after_publish{}, wrong_sequence{};
  bool refuse_first_close{}, finish_on_query{};
  bool revoke_during_bind{};
  bool pending_retired_once{};
  owned_local_resource_session* reenter{};
  bool reentrant_pending{};
  native_model() { require(!current, "Native model must be single flight"); current = this; }
  ~native_model() { require(!callbacks.owner, "Native callback owner was not retired"); current = nullptr; }
  static std::uint32_t SOUI_LOCAL_RESOURCE_CALL bind(SouiLocalResourceEndpoint,
      std::uint64_t view, std::uint64_t document, const SouiLocalResourceConfig* config,
      SouiLocalResourceReceipt* out) noexcept {
    auto& self = *current;
    if (self.callbacks.owner) return SouiLocalPending;
    ++self.bind_calls;
    self.callbacks = config->callbacks;
    self.callbacks.retain(self.callbacks.owner);
    if (self.revoke_during_bind && self.reenter) self.reenter->revoke_admission();
    self.receipt = {};
    self.receipt.structSize = sizeof(self.receipt); self.receipt.abiVersion = SOUI_LOCAL_RESOURCE_ABI;
    self.receipt.viewGeneration = view;
    self.receipt.boundBrokerDocumentGeneration = document;
    self.receipt.documentReceipt = self.bind_calls;
    const auto base = std::string("https://") + config->hostNonce + ".soui-local.invalid/" +
        std::string(32, static_cast<char>('a' + self.bind_calls)) + "/";
    const auto entry = base + config->entryRelative;
    std::memcpy(self.receipt.virtualBase, base.c_str(), base.size() + 1);
    std::memcpy(self.receipt.entryUrl, entry.c_str(), entry.size() + 1);
    self.phase = SouiLocalReady; self.close_sequence = 0;
    *out = self.receipt;
    if (self.fail_bind_after_publish) { self.failed = true; return SouiLocalFailed; }
    return SouiLocalOk;
  }
  static std::uint32_t snapshot(SouiLocalResourceDrain* out) noexcept {
    auto& self = *current;
    *out = {};
    out->structSize = sizeof(*out); out->abiVersion = SOUI_LOCAL_RESOURCE_ABI;
    out->phase = self.phase; out->failed = self.failed;
    out->viewGeneration = self.receipt.viewGeneration;
    out->documentReceipt = self.receipt.documentReceipt;
    out->closeSequence = self.close_sequence + (self.wrong_sequence ? 1 : 0);
    out->activeOperations = self.active; out->callbackBorrows = self.borrows;
    out->queuedOperations = self.queued; out->borrowedBytes = self.borrowed;
    out->denied = self.denied;
    out->payloadHeld = self.callbacks.owner ? 1U : 0U;
    return self.failed ? SouiLocalFailed : self.phase == SouiLocalClosing ? SouiLocalPending : SouiLocalOk;
  }
  static std::uint32_t SOUI_LOCAL_RESOURCE_CALL close(SouiLocalResourceEndpoint,
      std::uint64_t document, SouiLocalResourceDrain* out) noexcept {
    auto& self = *current; ++self.close_calls;
    if (document != self.receipt.documentReceipt) return SouiLocalStale;
    if (self.refuse_first_close) {
      self.refuse_first_close = false;
      (void)snapshot(out);
      return SouiLocalPending; // Actual native lease refusal leaves Ready/closeSequence=0.
    }
    if (self.phase == SouiLocalReady) {
      self.phase = SouiLocalClosing;
      self.close_sequence = 1000 + document;
    }
    const auto status = snapshot(out);
    if (self.pending_retired_once && self.phase == SouiLocalPayloadRetired) {
      self.pending_retired_once = false;
      return SouiLocalPending;
    }
    return status;
  }
  static std::uint32_t SOUI_LOCAL_RESOURCE_CALL query(SouiLocalResourceEndpoint,
      std::uint64_t document, SouiLocalResourceDrain* out) noexcept {
    auto& self = *current; ++self.query_calls;
    if (document != self.receipt.documentReceipt) return SouiLocalStale;
    if (self.finish_on_query) {
      self.finish_on_query = false;
      if (self.reenter) {
        const auto nested = self.reenter->retire_on_engine(true);
        self.reentrant_pending = nested.code == "E_LOCAL_RESOURCE_PENDING" && !self.reenter->payload_retired();
      }
      self.finish(); // Model the native terminal release before the outer query publishes retirement.
    }
    const auto status = snapshot(out);
    if (self.pending_retired_once && self.phase == SouiLocalPayloadRetired) {
      self.pending_retired_once = false;
      return SouiLocalPending;
    }
    return status;
  }
  SouiLocalResourceApi api() const noexcept {
    return {sizeof(SouiLocalResourceApi), SOUI_LOCAL_RESOURCE_ABI, 7, bind, close, query};
  }
  void finish(bool failure = false) {
    failed = failed || failure;
    active = borrows = queued = 0; borrowed = 0;
    phase = SouiLocalPayloadRetired;
    if (callbacks.owner) {
      auto released = callbacks;
      callbacks = {};
      released.release(released.owner); ++releases;
    }
  }
};
native_model* native_model::current{};

void prepare(owned_local_resource_session& session, const resources& files) {
  require(session.prepare(files.root, {}, [](const local_resource_snapshot&) {
    return soui::sdk::host_result{true, {}, {}};
  }, GetTickCount64() + 5000).ok, "Actual publication worker must prepare the closed snapshot");
  require(session.publication_retired(), "Publication means actual thread exit, not just posted completion");
  require(!session.physically_retired(), "Completed publication still retains the actual SDK snapshot owner");
}
void* const module = reinterpret_cast<void*>(1);
void* const view = reinterpret_cast<void*>(2);
constexpr SouiLocalResourceEndpoint endpoint{7, 9};

void missing_capability_is_explicit() {
  resources files;
  owned_local_resource_session session;
  prepare(session, files);
  const auto result = session.bind_on_engine(module, view, endpoint, 11, 13);
  require(!result.ok && result.code == "E_LOCAL_RESOURCE_UNSUPPORTED", "Missing ABI must not fall back to file");
  require(session.payload_retired() && !session.admission_open(), "Missing ABI must not invent a native owner");
  require(session.release_unbound_publication(), "Unpublished snapshot must retire without native calls");
}

void publication_policy_failure_keeps_its_category() {
  resources files;
  owned_local_resource_session session;
  const auto result = session.prepare(files.root, {}, [](const local_resource_snapshot&) {
    return soui::sdk::host_result{false, "E_UNTRUSTED_FRAME", "Reject the forbidden frame surface"};
  }, GetTickCount64() + 5000);
  require(!result.ok && result.code == "E_UNTRUSTED_FRAME" && session.terminal_result().code == result.code,
      "Publication policy rejection must keep the original category through later cleanup");
  require(session.release_unbound_publication() && session.physically_retired() &&
      session.terminal_result().code == result.code, "Physical cleanup cannot replace the initial policy failure");
}

void published_bind_failure_still_retires() {
  resources files;
  native_model native; native.fail_bind_after_publish = true;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  const auto bound = session.bind_on_engine(module, view, endpoint, 11, 13);
  require(!bound.ok && !session.payload_retired() && native.callbacks.owner,
      "Failed bind with a published receipt must retain its native callback owner");
  const auto first = session.retire_on_engine(true);
  require(!first.ok && !session.payload_retired() && native.close_calls == 1,
      "Native FAILED must still begin original physical retirement");
  native.finish();
  const auto last = session.retire_on_engine(true);
  require(session.physically_retired() && !last.ok && last.code == bound.code && native.releases == 1,
      "Successful physical cleanup must preserve the first bind failure exactly");
}

void pending_borrows_and_failed_retirement() {
  resources files;
  native_model native;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  require(session.bind_on_engine(module, view, endpoint, 11, 13).ok, "Native owner must bind");
  native.active = 64; native.borrows = 128; native.queued = 64;
  native.borrowed = 128ULL * 1024 * 1024;
  const auto first = session.retire_on_engine(true);
  require(first.code == "E_LOCAL_RESOURCE_PENDING" && !session.failed() && !session.payload_retired(),
      "The real maximum callback nesting is valid pending data, not a malformed receipt");
  require(native.close_calls == 1 && native.query_calls == 0 && native.callbacks.owner,
      "One retirement poll must make one native call and keep payload while pending");
  native.failed = true;
  const auto failure = session.retire_on_engine(true);
  require(!failure.ok && session.failed() && !session.payload_retired() && native.query_calls == 1,
      "FAILED with active borrows cannot authorize payload release");
  native.finish();
  const auto retired = session.retire_on_engine(true);
  require(!retired.ok && session.physically_retired() && session.native_error() == SouiLocalFailed && native.releases == 1,
      "FAILED plus actual PayloadRetired preserves fault while allowing physical teardown");
  const auto calls = native.bind_calls;
  require(!session.bind_on_engine(module, view, endpoint, 11, 14).ok && native.bind_calls == calls,
      "A failed owner cannot be rebound into a successful new document");
}

void receipt_sequence_is_not_replaced() {
  resources files;
  native_model native;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  require(session.bind_on_engine(module, view, endpoint, 11, 13).ok, "Native owner must bind");
  (void)session.retire_on_engine(true);
  native.wrong_sequence = true;
  (void)session.retire_on_engine(true);
  require(session.failed() && !session.payload_retired() && native.callbacks.owner,
      "A different close sequence cannot release the original owner");
  native.wrong_sequence = false; native.finish();
  require(!session.retire_on_engine(true).ok && session.physically_retired(),
      "Later valid physical retirement must not erase a malformed earlier receipt");
}

void close_not_admitted_retries_original_begin() {
  resources files;
  native_model native; native.refuse_first_close = true;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  require(session.bind_on_engine(module, view, endpoint, 11, 13).ok, "Native owner must bind");
  require(session.retire_on_engine(true).code == "E_LOCAL_RESOURCE_PENDING" &&
      !session.failed() && native.close_calls == 1 && native.query_calls == 0,
      "Valid Pending/Ready/zero-sequence means close has not yet been admitted");
  require(session.retire_on_engine(true).code == "E_LOCAL_RESOURCE_PENDING" &&
      !session.failed() && native.close_calls == 2 && native.query_calls == 0,
      "The same owner must retry beginClose instead of querying Ready forever");
  native.finish();
  require(session.retire_on_engine(true).ok && session.physically_retired(), "The accepted close can then retire");
}

void release_reentrancy_keeps_outer_native_borrow() {
  resources files;
  native_model native;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  require(session.bind_on_engine(module, view, endpoint, 11, 13).ok, "Native owner must bind");
  (void)session.retire_on_engine(true);
  native.reenter = &session; native.finish_on_query = true;
  require(session.retire_on_engine(true).ok && session.physically_retired(), "The outer query must receive actual retirement");
  require(native.reentrant_pending && native.close_calls == 1 && native.query_calls == 1 && native.releases == 1,
      "A release callout cannot recursively query or retire its own still-executing native call");
}

void revoked_bind_cannot_reopen_admission() {
  resources files;
  native_model native;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  native.reenter = &session; native.revoke_during_bind = true;
  const auto result = session.bind_on_engine(module, view, endpoint, 11, 13);
  require(!result.ok && result.code == "E_LOCAL_RESOURCE_CANCELLED" && !session.admission_open() &&
      !session.payload_retired() && !session.accepts_context(4),
      "Revocation during opaque bind cannot be overwritten by a later admission commit");
  (void)session.retire_on_engine(true); native.finish();
  require(session.retire_on_engine(true).ok && session.physically_retired(),
      "Cancelled bind must still retire its actual published native owner");
}

void callback_bound_plus_one_is_rejected() {
  resources files;
  native_model native;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  require(session.bind_on_engine(module, view, endpoint, 11, 13).ok, "Native owner must bind");
  native.borrows = 129;
  (void)session.retire_on_engine(true);
  require(session.failed() && !session.payload_retired() && native.callbacks.owner,
      "An out-of-contract callback count cannot authorize payload retirement");
  native.finish();
  require(!session.retire_on_engine(true).ok && session.physically_retired(),
      "A later valid owner retirement keeps the malformed receipt failure sticky");
}

void pending_status_does_not_complete_a_retired_snapshot() {
  resources files;
  native_model native;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  require(session.bind_on_engine(module, view, endpoint, 11, 13).ok, "Native owner must bind");
  (void)session.retire_on_engine(true);
  native.finish(); native.pending_retired_once = true;
  require(session.retire_on_engine(true).code == "E_LOCAL_RESOURCE_PENDING" && !session.payload_retired() &&
      !session.physically_retired(), "Pending cannot be upgraded to successful drain from phase/counters alone");
  require(session.retire_on_engine(true).ok && session.physically_retired(),
      "A later accepted terminal query completes the original retirement fence");
}

void observed_owner_fault_revokes_before_retirement() {
  resources files;
  native_model native;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  require(session.bind_on_engine(module, view, endpoint, 11, 13).ok, "Native owner must bind");
  const auto entry = session.entry_url();
  require(session.allow_navigation(view, entry) &&
      session.acknowledge_context(view, entry, session.native_document_receipt(), 4), "Exact document must ACK");
  native.denied = 1;
  session.observe_on_engine();
  require(!session.failed() && session.accepts_context(4), "One rejected request is not a fatal native owner fault");
  native.failed = true; native.active = 1; native.queued = 1; native.borrowed = 13;
  session.observe_on_engine();
  require(session.failed() && !session.accepts_context(4) && !session.admission_open() &&
      native.callbacks.owner && native.close_calls == 0 && !session.physically_retired(),
      "Periodic observation revokes business scope but cannot drop native queued work or synthesize completion");
  (void)session.retire_on_engine(true);
  require(native.close_calls == 1 && native.callbacks.owner && !session.payload_retired(),
      "The original close path must still cancel and retire the faulted native owner");
  native.finish();
  require(!session.retire_on_engine(true).ok && session.physically_retired() && session.native_error() == SouiLocalFailed,
      "Actual fault retirement must preserve failure after physical owners disappear");
}

void reload_rotates_only_after_actual_retirement() {
  resources files;
  native_model native;
  owned_local_resource_session session;
  prepare(session, files); session.test_api(native.api());
  require(session.bind_on_engine(module, view, endpoint, 11, 13).ok, "Initial owner must bind");
  const auto original_entry = session.entry_url();
  const auto original_native = session.native_document_receipt();
  require(session.allow_navigation(view, original_entry), "Only the explicitly armed initial load may navigate");
  require(!session.allow_navigation(view, original_entry), "A second same-URL load cannot bypass nonce rotation");
  require(!session.acknowledge_context(view, original_entry, original_native + 1, 4), "Wrong native receipt cannot ACK");
  require(!session.acknowledge_context(reinterpret_cast<void*>(3), original_entry, original_native, 4), "Foreign view cannot ACK");
  require(session.acknowledge_context(view, original_entry, original_native, 4), "Exact three-part document identity may ACK");
  require(session.accepts_context(view, 4) && !session.accepts_context(view, 6), "Bridge binds exact completed context receipt");
  require(session.allow_navigation(view, original_entry + "#route"), "Completed document may perform same-entry hash navigation");
  require(!session.allow_navigation(view, original_entry + "?other=1"), "Query aliases do not inherit the Bridge");
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(1);
  std::uint32_t length = 1, mime = 999;
  require(native.callbacks.lookup(native.callbacks.owner, "empty.txt", 9, &bytes, &length, &mime) == 1 &&
      !bytes && !length && mime == SouiLocalPlain, "Empty found resource must remain zero bytes");
  require(native.callbacks.lookup(native.callbacks.owner, "absent.json", 11, &bytes, &length, &mime) == 0,
      "Missing resource remains absent for actual native 404");
  require(session.begin_reload(), "The UI must explicitly admit one original reload transition");
  require(!session.begin_reload(), "A pending reload cannot create a second successor permit");
  require(!session.accepts_context(view, 4), "UI revocation closes Bridge before any Engine continuation");
  require(session.retire_on_engine(false).code == "E_LOCAL_RESOURCE_PENDING" && native.bind_calls == 1,
      "Pending reload cannot publish a successor owner");
  native.finish();
  require(session.retire_on_engine(false).ok, "Old payload must actually retire before rebind");
  require(session.bind_on_engine(module, view, endpoint, 11, 14).ok && native.bind_calls == 2,
      "A successful retired owner can publish one next document");
  require(session.entry_url() != original_entry && session.native_document_receipt() != original_native,
      "Both new URL nonce and native receipt must differ");
  require(!session.trusted_entry(view, original_entry) && !session.accepts_context(view, 4),
      "Old URL or context cannot be upgraded to the new owner");
  const auto next_entry = session.entry_url();
  require(session.allow_navigation(view, next_entry) &&
      session.acknowledge_context(view, next_entry, session.native_document_receipt(), 8),
      "New document requires its own load arm and completed ACK");
  (void)session.retire_on_engine(true); native.finish();
  require(session.retire_on_engine(true).ok && native.releases == 2, "Each accepted native payload owner releases exactly once");
}

void timed_out_worker_remains_owned() {
  resources files;
  HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  require(entered && release, "Publication gate events must be created");
  std::atomic<unsigned> visits{0};
  owned_local_resource_session session;
  const auto result = session.prepare(files.root, {}, [&](const local_resource_snapshot&) {
    ++visits; SetEvent(entered);
    const auto wait = WaitForSingleObject(release, 5000);
    return soui::sdk::host_result{wait == WAIT_OBJECT_0, "E_TEST_GATE", "Publication gate did not release"};
  }, GetTickCount64());
  require(!result.ok && result.code == "E_LOCAL_RESOURCE_STARTUP_TIMEOUT", "Preparation uses the original expired deadline");
  require(WaitForSingleObject(entered, 2000) == WAIT_OBJECT_0, "The actual retained publication worker must enter validation");
  const auto handle = static_cast<HANDLE>(session.test_publication_handle());
  require(handle && !session.publication_retired() && !session.release_unbound_publication(),
      "Timeout must retain the worker handle and its snapshot; it cannot invent retirement");
  require(session.prepare(files.root, {}, [](const local_resource_snapshot&) {
    return soui::sdk::host_result{true, {}, {}};
  }, GetTickCount64() + 5000).code == "E_LOCAL_RESOURCE_ALREADY_PREPARING" && visits == 1,
      "A pending owner must not start a second publication worker");
  SetEvent(release);
  require(WaitForSingleObject(handle, 2000) == WAIT_OBJECT_0 && session.publication_retired(),
      "Only actual thread signalling permits publication retirement");
  require(session.release_unbound_publication() && !session.terminal_result().ok,
      "Actual worker completion permits cleanup but cannot erase startup timeout");
  CloseHandle(release); CloseHandle(entered);
}
} // namespace

int main() {
  missing_capability_is_explicit();
  publication_policy_failure_keeps_its_category();
  published_bind_failure_still_retires();
  pending_borrows_and_failed_retirement();
  receipt_sequence_is_not_replaced();
  close_not_admitted_retries_original_begin();
  release_reentrancy_keeps_outer_native_borrow();
  revoked_bind_cannot_reopen_admission();
  callback_bound_plus_one_is_rejected();
  pending_status_does_not_complete_a_retired_snapshot();
  observed_owner_fault_revokes_before_retirement();
  reload_rotates_only_after_actual_retirement();
  timed_out_worker_remains_owned();
  std::puts("Local-resource Host integration: 13 cases passed");
}

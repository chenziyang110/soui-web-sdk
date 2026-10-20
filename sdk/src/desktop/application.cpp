#include <soui/desktop/app.h>
#include "backend.h"
#include "protocol.h"
#include "../runtime/platform/secure_random.h"
#include "../runtime/platform/strict_utf8.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace soui::desktop {
namespace {
std::atomic<bool> process_application{false};
bool valid_text(std::string_view value, std::size_t limit) {
  return value.size() <= limit && value.find('\0') == std::string_view::npos &&
    soui::runtime::detail::valid_utf8(value);
}
result unavailable() { return result::failure("closed", "The desktop application is not active."); }
struct application_state : std::enable_shared_from_this<application_state> {
  options settings;
  callbacks handlers;
  std::unique_ptr<detail::backend> native;
  const std::thread::id owner{std::this_thread::get_id()};
  std::atomic<bool> accepting{false};
  bool initialized{false};
  bool owns_process_slot{false};
  bool loop_started{false};
  bool notified_closed{false};
  bool ready{false};
  std::string token;
  detail::document current;
  std::uint64_t generation{0};
  std::uint64_t last_request{0};
  struct queued_request { std::string payload; std::string source; detail::message_frame frame; };
  std::deque<queued_request> requests;
  std::deque<result> errors;
  std::deque<std::function<void()>> work_items;
  std::atomic<std::size_t> pending_work{0};
  unsigned callback_depth{0};
  bool draining{false};
  bool pending_closed{false};
  bool pending_ready{false};
  std::uint64_t ready_generation{0};

  result check_thread() const {
    if (std::this_thread::get_id() != owner)
      return result::failure("wrong_thread", "Use application.dispatch from worker threads.");
    return result::success();
  }
  result check() const {
    const auto thread = check_thread();
    if (!thread.ok) return thread;
    return initialized && accepting.load() ? result::success() : unavailable();
  }
  void report(result failure) noexcept {
    if (errors.size() < 8) errors.push_back(std::move(failure));
    drain();
  }
  void drain() noexcept {
    if (callback_depth || draining) return;
    draining = true;
    while (true) {
      if (pending_closed) {
        pending_closed = false;
        requests.clear();
        work_items.clear();
        pending_ready = false;
        if (handlers.closed) {
          ++callback_depth;
          try { handlers.closed(); } catch (...) {}
          --callback_depth;
        }
      } else if (!errors.empty()) {
        auto error = std::move(errors.front()); errors.pop_front();
        if (handlers.error) {
          ++callback_depth;
          try { handlers.error(error); } catch (...) {}
          --callback_depth;
        }
      } else if (pending_ready) {
        pending_ready = false;
        if (accepting.load() && ready_generation == generation && handlers.ready) {
          ++callback_depth;
          try { handlers.ready(); }
          catch (...) { report(result::failure("callback_failed", "The ready callback failed.")); }
          --callback_depth;
        }
      } else if (!requests.empty()) {
        auto request = std::move(requests.front()); requests.pop_front();
        receive(request.payload, request.source, request.frame);
      } else if (!work_items.empty()) {
        auto work = std::move(work_items.front()); work_items.pop_front();
        pending_work.fetch_sub(1);
        if (accepting.load()) {
          ++callback_depth;
          try { work(); }
          catch (...) { report(result::failure("callback_failed", "A dispatched callback failed.")); }
          --callback_depth;
        }
      } else break;
    }
    draining = false;
  }
  void on_closed() noexcept {
    accepting.store(false);
    ready = false;
    token.clear();
    ++generation;
    if (!notified_closed) {
      notified_closed = true;
      pending_closed = true;
      drain();
    }
  }
  bool allowed(std::string_view uri) const {
    return accepting.load() && !token.empty() && detail::same_document_uri(uri, current.uri);
  }
  result deliver(nlohmann::json value) {
    if (!accepting.load() || token.empty()) return unavailable();
    value["v"] = 1;
    value["token"] = token;
    const auto text = value.dump();
    if (text.size() > detail::maximum_response_bytes) {
      return result::failure("response_too_large", "Native Bridge response exceeds its byte limit.");
    }
    auto sent = native->evaluate("window.__souiDesktopDeliver&&window.__souiDesktopDeliver(JSON.parse(" +
      nlohmann::json(text).dump() + "));");
    return sent;
  }
  void receive(std::string_view payload, std::string_view source, detail::message_frame frame) {
    if (!accepting.load() || !allowed(source) ||
        frame == detail::message_frame::subframe ||
        (frame == detail::message_frame::unknown && native->has_message_frame_metadata())) return;
    if (callback_depth) {
      if (payload.size() <= detail::maximum_request_bytes && source.size() <= 65536 && requests.size() < 128)
        requests.push_back({std::string(payload), std::string(source), frame});
      return;
    }
    try {
      const auto input = detail::parse_bounded_json(payload, detail::maximum_request_bytes);
      if (!input.is_object() || input.size() != 5 ||
          !input.contains("v") || input.at("v") != 1 ||
          !input.contains("token") || !input.at("token").is_string() ||
          input.at("token").get_ref<const std::string&>() != token ||
          !input.contains("id") || !input.at("id").is_number_unsigned() ||
          !input.contains("method") || !input.at("method").is_string() ||
          !input.contains("params")) return;
      const auto id = input.at("id").get<std::uint64_t>();
      const auto& method = input.at("method").get_ref<const std::string&>();
      if (id <= last_request || id > detail::maximum_request_id || !detail::valid_name(method)) return;
      last_request = id;
      const auto request_generation = generation;
      nlohmann::json response{{"id", id}};
      if (!handlers.invoke) {
        response["ok"] = false;
        response["error"] = {{"code", "method_not_registered"}, {"message", "No native method handler is registered."}};
      } else {
        try {
          ++callback_depth;
          std::string returned;
          try { returned = handlers.invoke(method, input.at("params").dump()); }
          catch (...) { --callback_depth; throw; }
          --callback_depth;
          response["result"] = detail::parse_bounded_json(returned, detail::maximum_response_bytes / 2);
          response["ok"] = true;
        } catch (...) {
          response["ok"] = false;
          response["error"] = {{"code", "handler_failed"}, {"message", "The application rejected this request."}};
        }
      }
      // A handler can close or navigate. Never send its result to a later page.
      if (generation == request_generation && accepting.load()) {
        auto sent = deliver(std::move(response));
        if (!sent.ok) report(std::move(sent));
      }
      drain();
    } catch (...) {
      // Invalid/untrusted envelopes have no right to obtain an error response.
      report(result::failure("invalid_bridge_message", "Discarded an invalid or oversized Bridge envelope."));
    }
  }
  result navigate(const std::filesystem::path& relative) {
    const auto usable = check();
    if (!usable.ok) return usable;
    detail::document next;
    const auto resolved = detail::resolve_document(settings.resource_root, relative, next);
    if (!resolved.ok) return resolved;
    unsigned char bytes[32]{};
    token.clear();
    ready = false;
    ++generation;
    last_request = 0;
    if (!soui::runtime::detail::secure_random(bytes, sizeof(bytes)))
      return result::failure("entropy_unavailable", "Cannot authorize a document without system entropy.");
    constexpr char digits[] = "0123456789abcdef";
    for (const auto byte : bytes) { token += digits[byte >> 4U]; token += digits[byte & 15U]; }
    current = std::move(next);
    auto loaded = native->load(current.uri, detail::make_preload(token));
    if (!loaded.ok) token.clear();
    return loaded;
  }
};
}  // namespace

struct application::implementation {
  std::shared_ptr<application_state> state = std::make_shared<application_state>();
};

application::application(options settings, callbacks handlers) : impl_(std::make_unique<implementation>()) {
  impl_->state->settings = std::move(settings);
  impl_->state->handlers = std::move(handlers);
}
application::~application() {
  auto state = impl_->state;
  // Native UI objects cannot be safely destroyed from a worker. This is a
  // documented contract, not a silent cross-thread teardown or leaked owner.
  if (state->native && std::this_thread::get_id() != state->owner) std::terminate();
  state->handlers = {};
  state->accepting.store(false);
  state->token.clear();
  if (state->native) { (void)state->native->close(); state->native.reset(); }
  if (state->owns_process_slot) process_application.store(false);
}

result application::initialize() {
  auto state = impl_->state;
  const auto thread = state->check_thread();
  if (!thread.ok) return thread;
  if (state->initialized || state->native) return result::failure("already_initialized", "Initialize once per application.");
  if (!valid_text(state->settings.title, 4096) || state->settings.width < 64 || state->settings.height < 64 ||
      state->settings.width > 16384 || state->settings.height > 16384)
    return result::failure("invalid_options", "Window title or dimensions are invalid.");
  detail::document initial;
  auto verified = detail::resolve_document(state->settings.resource_root, state->settings.entry, initial);
  if (!verified.ok) return verified;
  std::error_code error;
  state->settings.resource_root = std::filesystem::canonical(state->settings.resource_root, error);
  if (error) return result::failure("invalid_root", "Cannot canonicalize the resource root.");
  bool expected = false;
  if (!process_application.compare_exchange_strong(expected, true))
    return result::failure("application_exists", "Only one desktop application is supported per process.");
  state->owns_process_slot = true;
  state->native = detail::make_backend();
  if (!state->native) return result::failure("not_supported", "No desktop backend was built.");
  const std::weak_ptr<application_state> weak = state;
  detail::backend_events events;
  events.allow_navigation = [weak](std::string_view uri) {
    const auto live = weak.lock(); return live && live->allowed(uri);
  };
  events.message = [weak](std::string_view payload, std::string_view uri, detail::message_frame frame) {
    if (const auto live = weak.lock()) live->receive(payload, uri, frame);
  };
  events.loaded = [weak](std::string_view uri) {
    if (const auto live = weak.lock(); live && live->allowed(uri)) {
      live->ready = true;
      live->pending_ready = true;
      live->ready_generation = live->generation;
      live->drain();
    }
  };
  events.closed = [weak]() { if (const auto live = weak.lock()) live->on_closed(); };
  events.reload = [weak]() {
    if (const auto live = weak.lock(); live && live->accepting.load()) {
      auto restarted = live->navigate(live->current.path.lexically_relative(live->settings.resource_root));
      if (!restarted.ok) live->report(std::move(restarted));
    }
  };
  events.error = [weak](result failure) { if (const auto live = weak.lock()) live->report(std::move(failure)); };
  auto created = state->native->create(state->settings, std::move(events));
  if (!created.ok) return created;
  state->initialized = true;
  state->accepting.store(true);
  auto navigation = state->navigate(state->settings.entry);
  if (!navigation.ok) { state->accepting.store(false); (void)state->native->close(); }
  return navigation;
}
result application::run() {
  auto state = impl_->state;
  const auto checked = state->check();
  if (!checked.ok) return checked;
  if (state->loop_started) return result::failure("already_running", "The application event loop runs only once.");
  state->loop_started = true;
  return state->native->run();
}
result application::close() {
  auto state = impl_->state;
  const auto thread = state->check_thread();
  if (!thread.ok) return thread;
  if (!state->initialized) return unavailable();
  state->accepting.store(false);
  state->ready = false;
  state->token.clear();
  ++state->generation;
  return state->native->close();
}
result application::navigate(const std::filesystem::path& relative) { return impl_->state->navigate(relative); }
result application::reload() {
  auto state = impl_->state;
  const auto checked = state->check();
  if (!checked.ok) return checked;
  return state->navigate(state->current.path.lexically_relative(state->settings.resource_root));
}
result application::emit(std::string_view event, std::string_view value) {
  auto state = impl_->state;
  const auto checked = state->check();
  if (!checked.ok) return checked;
  if (!state->ready) return result::failure("document_not_ready", "Wait for the ready callback before emitting events.");
  if (!detail::valid_name(event)) return result::failure("invalid_event", "Event name must be bounded ASCII.");
  try {
    return state->deliver({{"event", event}, {"value", detail::parse_bounded_json(value, detail::maximum_response_bytes / 2)}});
  } catch (...) { return result::failure("invalid_json", "Event value is not bounded JSON."); }
}
result application::dispatch(std::function<void()> work) {
  auto state = impl_->state;
  if (!work) return result::failure("invalid_callback", "A work callback is required.");
  if (!state->accepting.load()) return unavailable();
  if (state->pending_work.fetch_add(1) >= 1024) {
    state->pending_work.fetch_sub(1);
    return result::failure("queue_full", "The application dispatch queue is full.");
  }
  const std::weak_ptr<application_state> weak = state;
  auto queued = state->native->dispatch([weak, work = std::move(work)]() mutable {
    if (const auto live = weak.lock(); live && live->accepting.load()) {
      live->work_items.push_back(std::move(work));
      live->drain();
    }
  });
  if (!queued.ok) state->pending_work.fetch_sub(1);
  return queued;
}
result application::set_title(std::string_view title) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  if (!valid_text(title, 4096)) return result::failure("invalid_title", "Title must be bounded UTF-8.");
  return state->native->set_title(title);
}
result application::resize(int width, int height) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  if (width < 64 || height < 64 || width > 16384 || height > 16384)
    return result::failure("invalid_size", "Window dimensions must be between 64 and 16384.");
  return state->native->resize(width, height);
}
result application::show(bool visible) {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->show(visible) : checked;
}
result application::focus() {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->focus() : checked;
}
value_result application::clipboard_read() {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->clipboard_read() : value_result{checked, {}};
}
result application::clipboard_write(std::string_view text) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  if (!valid_text(text, 1048576)) return result::failure("invalid_text", "Clipboard text must be bounded UTF-8.");
  return state->native->clipboard_write(text);
}
value_result application::choose_directory() {
  auto state = impl_->state; const auto checked = state->check();
  return checked.ok ? state->native->choose_directory() : value_result{checked, {}};
}
capabilities application::get_capabilities() const {
  const auto state = impl_->state;
  if (!state->check_thread().ok || !state->native) return {};
  return state->native->get_capabilities();
}
}  // namespace soui::desktop

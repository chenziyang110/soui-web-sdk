#include <soui/desktop/app.h>
#include "backend.h"
#include "protocol.h"
#include "../runtime/platform/secure_random.h"
#include "../runtime/platform/strict_utf8.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <limits>
#include <set>
#include <thread>

namespace soui::desktop {
namespace {
std::atomic<bool> process_application{false};
std::atomic<window_id> next_window_id{1};
bool valid_text(std::string_view value, std::size_t limit) {
  return value.size() <= limit && value.find('\0') == std::string_view::npos &&
    soui::runtime::detail::valid_utf8(value);
}
result unavailable() { return result::failure("closed", "The desktop application is not active."); }
result validate_menu(const std::vector<menu_item>& items, bool empty_allowed) {
  if (items.size() > 64 || (!empty_allowed && items.empty()))
    return result::failure("invalid_menu", "A popup requires 1..64 items; a tray allows 0..64.");
  std::set<std::string> ids;
  for (const auto& item : items) {
    if (item.separator) {
      if (!item.id.empty() || !item.label.empty())
        return result::failure("invalid_menu", "Separators must have empty ID and label.");
    } else if (!detail::valid_name(item.id) || item.label.empty() || !valid_text(item.label, 4096) ||
               !ids.insert(item.id).second) {
      return result::failure("invalid_menu", "Menu IDs must be unique and labels bounded UTF-8.");
    }
  }
  return result::success();
}
std::set<std::string> enabled_commands(const std::vector<menu_item>& items) {
  std::set<std::string> ids;
  for (const auto& item : items) if (!item.separator && item.enabled) ids.insert(item.id);
  return ids;
}
struct application_state;
struct application_group {
  std::vector<std::weak_ptr<application_state>> windows;
  std::weak_ptr<application_state> root;
  unsigned callback_depth{0};
  bool draining{false};
  bool quitting{false};
  bool stopped{false};
  bool loop_started{false};
  bool seen_window{false};
  std::size_t open_count() const;
  void drain() noexcept;
  void stop_if_empty() noexcept;
  result quit();
};
struct application_state : std::enable_shared_from_this<application_state> {
  options settings;
  callbacks handlers;
  std::unique_ptr<detail::backend> native;
  std::shared_ptr<application_group> group;
  window_id identity{0};
  bool is_root{true};
  const std::thread::id owner{std::this_thread::get_id()};
  std::atomic<bool> accepting{false};
  bool initialized{false};
  bool owns_process_slot{false};
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
  std::deque<std::string> commands;
  std::set<std::string> popup_commands;
  std::set<std::string> tray_commands;
  std::atomic<std::size_t> pending_work{0};
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
    if (group->callback_depth || draining) return;
    draining = true;
    while (true) {
      if (pending_closed) {
        pending_closed = false;
        requests.clear();
        work_items.clear();
        commands.clear();
        pending_ready = false;
        ++group->callback_depth;
        try { if (handlers.closed) handlers.closed(); } catch (...) {}
        try { if (handlers.lifecycle) handlers.lifecycle(identity, window_event::closed); } catch (...) {}
        --group->callback_depth;
      } else if (!errors.empty()) {
        auto error = std::move(errors.front()); errors.pop_front();
        if (handlers.error) {
          ++group->callback_depth;
          try { handlers.error(error); } catch (...) {}
          --group->callback_depth;
        }
      } else if (pending_ready) {
        pending_ready = false;
        if (accepting.load() && ready_generation == generation) {
          ++group->callback_depth;
          const auto active_generation = generation;
          try { if (handlers.ready) handlers.ready(); }
          catch (...) { report(result::failure("callback_failed", "The ready callback failed.")); }
          try {
            if (accepting.load() && generation == active_generation && handlers.lifecycle)
              handlers.lifecycle(identity, window_event::ready);
          } catch (...) { report(result::failure("callback_failed", "The lifecycle callback failed.")); }
          --group->callback_depth;
        }
      } else if (!commands.empty()) {
        auto command = std::move(commands.front()); commands.pop_front();
        if (accepting.load() && command_allowed(command) && handlers.command) {
          ++group->callback_depth;
          try { handlers.command(command); }
          catch (...) { report(result::failure("callback_failed", "A native command callback failed.")); }
          --group->callback_depth;
        }
      } else if (!requests.empty()) {
        auto request = std::move(requests.front()); requests.pop_front();
        receive(request.payload, request.source, request.frame);
      } else if (!work_items.empty()) {
        auto work = std::move(work_items.front()); work_items.pop_front();
        pending_work.fetch_sub(1);
        if (accepting.load()) {
          ++group->callback_depth;
          try { work(); }
          catch (...) { report(result::failure("callback_failed", "A dispatched callback failed.")); }
          --group->callback_depth;
        }
      } else break;
    }
    draining = false;
    group->drain();
  }
  bool command_allowed(std::string_view command) const {
    const std::string value(command);
    return popup_commands.count(value) || tray_commands.count(value);
  }
  void on_command(std::string_view command) {
    if (!accepting.load() || !detail::valid_name(command) || !command_allowed(command) ||
        commands.size() >= 128) return;
    commands.emplace_back(command);
    drain();
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
    group->stop_if_empty();
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
    if (group->callback_depth) {
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
          ++group->callback_depth;
          std::string returned;
          try { returned = handlers.invoke(method, input.at("params").dump()); }
          catch (...) { --group->callback_depth; throw; }
          --group->callback_depth;
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
  result close() {
    if (!initialized) return unavailable();
    accepting.store(false);
    ready = false;
    token.clear();
    ++generation;
    return native->close();
  }
};

std::size_t application_group::open_count() const {
  std::size_t count = 0;
  for (const auto& weak : windows)
    if (const auto state = weak.lock(); state && state->initialized && !state->notified_closed) ++count;
  return count;
}
void application_group::drain() noexcept {
  if (draining || callback_depth) return;
  draining = true;
  bool progressed;
  do {
    progressed = false;
    const auto snapshot = windows; // A callback can create another window.
    for (const auto& weak : snapshot) if (const auto state = weak.lock(); state && !state->draining) {
      if (state->pending_closed || state->pending_ready || !state->errors.empty() ||
          !state->commands.empty() || !state->requests.empty() || !state->work_items.empty()) {
        progressed = true;
        state->drain();
      }
    }
  } while (progressed && !callback_depth);
  draining = false;
}
void application_group::stop_if_empty() noexcept {
  if (!seen_window || stopped || open_count()) return;
  stopped = true;
  quitting = true;
  if (const auto owner = root.lock(); owner && owner->native) {
    try {
      const auto stopped_result = owner->native->quit_loop();
      if (!stopped_result.ok) owner->report(stopped_result);
    } catch (...) {}
  }
}
result application_group::quit() {
  quitting = true;
  result status = result::success();
  const auto snapshot = windows;
  for (const auto& weak : snapshot) if (const auto state = weak.lock();
      state && state->initialized && !state->notified_closed) {
    const auto closed = state->close();
    if (!closed.ok && status.ok) status = closed;
  }
  stop_if_empty();
  return status;
}
}  // namespace

struct application::implementation {
  std::shared_ptr<application_state> state = std::make_shared<application_state>();
};

application::application(options settings, callbacks handlers) : impl_(std::make_unique<implementation>()) {
  impl_->state->group = std::make_shared<application_group>();
  impl_->state->group->root = impl_->state;
  impl_->state->settings = std::move(settings);
  impl_->state->handlers = std::move(handlers);
}
application::application(std::unique_ptr<implementation> value) : impl_(std::move(value)) {}
application::~application() {
  auto state = impl_->state;
  // Native UI objects cannot be safely destroyed from a worker. This is a
  // documented contract, not a silent cross-thread teardown or leaked owner.
  if (state->native && std::this_thread::get_id() != state->owner) std::terminate();
  state->handlers = {};
  if (state->is_root) {
    for (const auto& weak : state->group->windows)
      if (const auto child = weak.lock()) child->handlers = {};
    (void)state->group->quit();
  }
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
  if (state->group->quitting) return unavailable();
  if (state->is_root) {
    bool expected = false;
    if (!process_application.compare_exchange_strong(expected, true))
      return result::failure("application_exists", "Only one desktop application group is supported per process.");
    state->owns_process_slot = true;
  }
  auto identity = next_window_id.load();
  do {
    if (identity == std::numeric_limits<window_id>::max())
      return result::failure("window_id_exhausted", "Window identifiers cannot be reused.");
  } while (!next_window_id.compare_exchange_weak(identity, identity + 1));
  state->identity = identity;
  auto& windows = state->group->windows;
  windows.erase(std::remove_if(windows.begin(), windows.end(), [](const auto& weak) { return weak.expired(); }), windows.end());
  windows.push_back(state);
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
  events.command = [weak](std::string_view command) { if (const auto live = weak.lock()) live->on_command(command); };
  events.quit_requested = [weak]() { if (const auto live = weak.lock()) (void)live->group->quit(); };
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
  state->group->seen_window = true;
  if (state->notified_closed) {
    state->group->stop_if_empty();
    return result::failure("closed", "The native window closed during initialization.");
  }
  state->accepting.store(true);
  auto navigation = state->navigate(state->settings.entry);
  if (!navigation.ok) { state->accepting.store(false); (void)state->native->close(); }
  return navigation;
}
result application::run() {
  auto state = impl_->state;
  const auto checked = state->check_thread();
  if (!checked.ok) return checked;
  if (!state->is_root) return result::failure("not_loop_owner", "Run the root application event loop.");
  if (!state->initialized || !state->group->open_count()) return unavailable();
  if (state->group->loop_started) return result::failure("already_running", "The application event loop runs only once.");
  state->group->loop_started = true;
  return state->native->run();
}
result application::close() {
  auto state = impl_->state;
  const auto thread = state->check_thread();
  if (!thread.ok) return thread;
  return state->close();
}
result application::quit() {
  auto state = impl_->state;
  const auto checked = state->check_thread();
  if (!checked.ok) return checked;
  if (!state->initialized) return unavailable();
  return state->group->quit();
}
window_result application::create_window(options settings, callbacks handlers) {
  auto state = impl_->state;
  const auto checked = state->check_thread();
  if (!checked.ok) return {checked, {}};
  if (!state->initialized || state->group->quitting || !state->group->open_count())
    return {unavailable(), {}};
  if (state->group->open_count() >= 32)
    return {result::failure("window_limit", "The application supports at most 32 live windows."), {}};
  auto value = std::make_unique<implementation>();
  value->state->settings = std::move(settings);
  value->state->handlers = std::move(handlers);
  value->state->group = state->group;
  value->state->is_root = false;
  auto child = std::shared_ptr<application>(new application(std::move(value)));
  auto created = child->initialize();
  if (!created.ok) return {std::move(created), {}};
  return {result::success(), std::move(child)};
}
window_id application::id() const noexcept { return impl_->state->identity; }
std::vector<window_id> application::window_ids() const {
  const auto state = impl_->state;
  if (!state->check_thread().ok) return {};
  std::vector<window_id> ids;
  for (const auto& weak : state->group->windows)
    if (const auto member = weak.lock(); member && member->accepting.load()) ids.push_back(member->identity);
  return ids;
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
result application::popup_menu(const std::vector<menu_item>& items) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  const auto valid = validate_menu(items, false);
  if (!valid.ok) return valid;
  auto previous = std::move(state->popup_commands);
  state->popup_commands = enabled_commands(items);
  auto shown = state->native->popup_menu(items);
  if (!shown.ok) state->popup_commands = std::move(previous);
  return shown;
}
result application::set_tray(const tray_options& settings) {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  const auto valid = validate_menu(settings.menu, true);
  if (!valid.ok) return valid;
  if (settings.label.empty() || !valid_text(settings.label, 32) || !valid_text(settings.tooltip, 4096))
    return result::failure("invalid_tray", "Tray label and tooltip must be bounded UTF-8.");
  auto previous = std::move(state->tray_commands);
  state->tray_commands = enabled_commands(settings.menu);
  auto changed = state->native->set_tray(settings);
  if (!changed.ok) state->tray_commands = std::move(previous);
  return changed;
}
result application::clear_tray() {
  auto state = impl_->state; const auto checked = state->check();
  if (!checked.ok) return checked;
  auto cleared = state->native->clear_tray();
  if (cleared.ok) state->tray_commands.clear();
  return cleared;
}
capabilities application::get_capabilities() const {
  const auto state = impl_->state;
  if (!state->check_thread().ok || !state->native) return {};
  return state->native->get_capabilities();
}
}  // namespace soui::desktop

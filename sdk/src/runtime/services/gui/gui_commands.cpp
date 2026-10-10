#include "gui_commands_internal.h"
#include <json.hpp>
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace soui::runtime::detail {
namespace {
using json = nlohmann::json;
using clock_type = std::chrono::steady_clock;
constexpr unsigned capacity_limit{16};
constexpr std::uint64_t max_sequence{9007199254740991ULL};
std::string reject(const char* code) { return json{{"error", code}, {"message", "GUI command rejected."}}.dump(); }
bool identifier(const std::string& value) {
  return !value.empty() && value.size() <= 128 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '.' || c == '_' || c == '-' || c == ':';
  });
}
json decode(std::string_view value, std::size_t limit = 32768) {
  if (value.empty() || value.size() > limit) throw std::invalid_argument("GUI size");
  auto parsed = json::parse(value.begin(), value.end(), [](int depth, json::parse_event_t, json&) {
    if (depth > 8) throw std::invalid_argument("GUI depth"); return true;
  });
  if (!parsed.is_object()) throw std::invalid_argument("GUI object");
  (void)parsed.dump(); // Exact valid UTF-8, including fields a projection ignores.
  return parsed;
}
std::string identity() {
  GUID value{}; wchar_t encoded[40]{};
  if (FAILED(CoCreateGuid(&value)) || !StringFromGUID2(value, encoded, 40))
    throw std::runtime_error("GUI identity");
  std::string result{"gui:"};
  for (auto c : encoded) if (c && c != L'{' && c != L'}') result += static_cast<char>(c);
  return result;
}
json string_schema(unsigned max) { return {{"type","string"},{"maxLength",max}}; }
json object_schema(json properties = json::object(), json required = json::array()) {
  return {{"type","object"},{"properties",std::move(properties)},
      {"required",std::move(required)},{"additionalProperties",false}};
}
std::string contract(const std::string& title, const json& input, const json& output) {
  return json{{"protocol","soui.action/1"},{"title",title},{"description","Bounded GUI command."},
      {"inputSchema",input},{"outputSchema",output}}.dump();
}
json result_schema() {
  return object_schema({{"confirmed",{{"type","boolean"}}},{"guiResultJson",string_schema(32768)},
      {"effectState",{{"type","string"},{"enum",{"none","unused","committed","rejected","unknown"}},{"maxLength",16}}},
      {"effectResultJson",string_schema(32768)}}, {"confirmed","guiResultJson","effectState","effectResultJson"});
}
bool ends(const std::string& value, const char* tail) {
  const std::string suffix{tail}; return value.size() >= suffix.size() &&
      value.compare(value.size()-suffix.size(),suffix.size(),suffix)==0;
}
} // namespace

gui_job_context::~gui_job_context() { if (capacity) capacity->used.fetch_sub(1, std::memory_order_acq_rel); }
bool gui_job_context::authorizes_effect(const bridge_request& request) const {
  return consumption.load(std::memory_order_acquire) == 1 && !cancelled.load(std::memory_order_acquire) &&
      document && !document->closed.load(std::memory_order_acquire) && document->page_scope &&
      document->page_scope->active() && clock_type::now() < deadline &&
      agent_scope && agent_scope->active() && agent_scope->matches(request.session_id, request.origin) &&
      agent_scope->allows_local_method(row->effect_method, effect_capability) &&
      request.request_id == request_id && request.method == row->effect_method && request.params_json == effect_params &&
      request.token.empty() && !request.native_user_gesture;
}
gui_commands_state::gui_commands_state(capability_registry& r, bridge_transport& t, task_service& s)
    : registry{r}, transport{t}, tasks{s} { pending.reserve(capacity_limit); effects.reserve(capacity_limit); }

std::shared_ptr<gui_commands_state> gui_commands_state::create(capability_registry& registry,
    bridge_transport& transport, task_service& tasks, std::vector<gui_command_definition> rows) {
  try {
    if (rows.empty() || rows.size() > capacity_limit || registry.contains("gui.bind") ||
        registry.contains("gui.effect") || registry.contains("gui.ack")) return {};
    auto state = std::shared_ptr<gui_commands_state>(new gui_commands_state(registry,transport,tasks));
    const std::weak_ptr<gui_commands_state> weak{state};
    capability_registry candidate = registry;
    const auto id = string_schema(128), data = string_schema(32768);
    const auto accepted = object_schema({{"accepted",{{"type","boolean"}}}}, {"accepted"});
    if (!candidate.register_action("gui.bind","gui.bind",contract("gui.bind",object_schema(),accepted),
        [weak](const bridge_request& r) { const auto s=weak.lock(); return s?s->page_bind(r):reject("E_GUI_CLOSED"); })) return {};
    const auto effect_input = object_schema({{"requestId",id},{"documentId",id},{"effectId",id},{"inputJson",data}},
        {"requestId","documentId","effectId","inputJson"});
    if (!candidate.register_action("gui.effect","gui.effect",contract("gui.effect",effect_input,
        object_schema({{"resultJson",data}}, {"resultJson"})), [weak](const bridge_request& r) {
          const auto s=weak.lock(); return s?s->page_effect(r):reject("E_GUI_CLOSED"); })) return {};
    const auto ack_input = object_schema({{"requestId",id},{"documentId",id},{"ok",{{"type","boolean"}}},{"resultJson",data}},
        {"requestId","documentId","ok","resultJson"});
    if (!candidate.register_action("gui.ack","gui.ack",contract("gui.ack",ack_input,accepted),
        [weak](const bridge_request& r) { const auto s=weak.lock(); return s?s->ack(r):reject("E_GUI_CLOSED"); })) return {};
    std::set<std::string> names;
    std::vector<gui_family_registration> registrations;
    for (auto& value : rows) {
      if (!identifier(value.family_name) || value.family_name.size()>112 || !identifier(value.resource) ||
          !identifier(value.id) || !names.insert(value.family_name).second || !value.project_result ||
          (value.operation!=gui_operation::perform && value.operation!=gui_operation::snapshot && value.operation!=gui_operation::wait_for) ||
          (value.operation==gui_operation::perform ? !identifier(value.action) : !value.action.empty()) ||
          (value.operation!=gui_operation::perform && !value.effect_method.empty())) return {};
      const auto input=decode(value.input_schema_json,action_contract::max_schema_bytes);
      const auto output=decode(value.result_schema_json,action_contract::max_schema_bytes);
      if (!action_contract::parse(contract(value.family_name,input,output))) return {};
      if (!value.effect_method.empty()) {
        const auto found=registry.methods_.find(value.effect_method);
        if (found==registry.methods_.end() || !found->second.action || !found->second.local_enabled ||
            value.effect_method.rfind("gui.",0)==0 || value.effect_method.rfind("task.",0)==0 ||
            value.effect_method.rfind("bridge.",0)==0 || names.count(value.effect_method)) return {};
      }
      const auto row=std::make_shared<const gui_command_definition>(std::move(value));
      auto hooks=std::make_shared<gui_family_hooks>();
      hooks->prepare=[weak,row](const bridge_request& r) { const auto s=weak.lock(); return s?s->prepare(row,r):nullptr; };
      hooks->run=[weak](const auto& context,const task_control& control) {
        const auto s=weak.lock(); return s?s->run(context,control):task_outcome{result_kind::failure,"{}","GUI closed."};
      };
      local_task_family_definition family{row->family_name,"soui.gui."+row->family_name,
          row->start_capability,row->query_capability,row->cancel_capability,
          row->input_schema_json,result_schema().dump(),[has_effect=!row->effect_method.empty()](const task_outcome& outcome) {
            // A worker cancelled before GUI run cannot have been dispatched.
            if (outcome.kind==result_kind::task_cancelled && outcome.result_json=="{}")
              return json{{"confirmed",false},{"guiResultJson","{}"},
                  {"effectState",has_effect?"unused":"none"},{"effectResultJson",""}}.dump();
            return outcome.result_json;
          }};
      registrations.push_back({std::move(family),std::move(hooks)});
    }
    if (!tasks.register_gui_families(candidate,std::move(registrations))) return {};
    // All controls/families are prepared; no callback or allocation after commit.
    registry.methods_.swap(candidate.methods_);
    return state;
  } catch (...) { return {}; }
}
void gui_commands_state::set_sink(std::shared_ptr<const task_service::event_sink> value) {
  std::lock_guard<std::mutex> lock(mutex); sink.swap(value);
}
void gui_commands_state::set_page(std::shared_ptr<const authorization_scope> scope,
    std::string session,std::string origin) {
  auto next=std::make_shared<gui_document>();
  next->page_scope=std::move(scope); next->session=std::move(session); next->origin=std::move(origin);
  next->id=identity();
  std::lock_guard<std::mutex> lock(mutex);
  if (closed.load(std::memory_order_acquire) || binding_started.load(std::memory_order_acquire)) return;
  document=std::move(next); page_ready=false;
}
std::string gui_commands_state::page_bind(const bridge_request& request) {
  std::lock_guard<std::mutex> lock(mutex);
  if (closed.load(std::memory_order_acquire) || !document || !request.authorization ||
      request.authorization!=document->page_scope || !request.authorization->active()) return reject("E_GUI_NOT_READY");
  page_ready=true; return R"({"accepted":true})"; // Page route, not native readiness/authority.
}
bool gui_commands_state::bind(std::string generation,std::function<bool()> callback) {
  if (std::this_thread::get_id()!=owner || !identifier(generation) || !callback || closed.load()) return false;
  auto proof=std::make_shared<const std::function<bool()>>(std::move(callback));
  std::shared_ptr<const std::function<bool()>> release;
  std::shared_ptr<gui_document> captured;
  { std::lock_guard<std::mutex> lock(mutex);
    if (polling || effect_borrows || !page_ready || !document) return false;
    polling=true; captured=document; }
  bool accepted=false;
  try {
    accepted=[&] {
      if (!captured->page_scope || !captured->page_scope->active() || captured->closed.load() || !(*proof)()) return false;
      bool expected=false;
      if (!binding_started.compare_exchange_strong(expected,true,std::memory_order_seq_cst)) return false;
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (closed.load(std::memory_order_acquire) || document!=captured || captured->closed.load() ||
            !captured->page_scope->active()) {
          closed.store(true,std::memory_order_release); return false;
        }
        captured->generation=std::move(generation); current=proof;
      }
      if (closed.load() || !captured->page_scope->active() || !(*proof)() || closed.load()) { close(); return false; }
      return true;
    }();
  } catch (...) { if (binding_started.load()) close(); }
  { std::lock_guard<std::mutex> lock(mutex); polling=false;
    if (closed.load()) { current.swap(release); accepted=false; } }
  // Both owner callbacks have returned; releasing their captures is safe here.
  return accepted;
}
void gui_commands_state::close() noexcept {
  closed.store(true,std::memory_order_seq_cst);
  std::vector<std::shared_ptr<gui_effect_request>> release;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (document) document->closed.store(true,std::memory_order_release);
    for (const auto& item:pending) if (const auto context=item.lock()) context->request_cancel();
    effects.swap(release);
  }
  // No application captures or request storage are released under the GUI lock.
  for (const auto& request:release) {
    unsigned queued=0;
    (void)request->phase.compare_exchange_strong(queued,3,std::memory_order_acq_rel);
    request->changed.notify_all();
  }
}
void gui_commands_state::shutdown_session(const std::string& session) noexcept {
  const bool before=binding_started.load(std::memory_order_seq_cst);
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (document && document->session==session) {
      document->closed.store(true,std::memory_order_release);
      if (!before) page_ready=false;
    }
  }
  if (before || binding_started.load(std::memory_order_seq_cst)) close();
}
std::shared_ptr<gui_job_context> gui_commands_state::prepare(
    const std::shared_ptr<const gui_command_definition>& row,const bridge_request& request) {
  auto input=decode(request.params_json).at("input").dump();
  auto context=std::shared_ptr<gui_job_context>(new gui_job_context);
  unsigned count=capacity->used.load(std::memory_order_acquire);
  bool reserved=false;
  for (unsigned attempt=0;attempt<64;++attempt) {
    if (count>=capacity_limit) return {};
    if (capacity->used.compare_exchange_weak(count,count+1,std::memory_order_acq_rel)) { reserved=true; break; }
  }
  if (!reserved) return {};
  context->capacity=capacity;
  context->agent_scope=request.authorization; context->row=row;
  context->agent_session=request.session_id; context->agent_origin=request.origin;
  context->request_id=identity(); context->effect_operation=identity(); context->approved_input=std::move(input);
  context->deadline=clock_type::now()+std::chrono::milliseconds(std::min<std::uint32_t>(request.timeout_ms,10000));
  if (!row->effect_method.empty()) {
    context->effect_capability=*registry.capability_for(row->effect_method);
    context->effect_is_family_start=ends(row->effect_method,".start") && registry.methods_.at(row->effect_method).local_only;
    context->effect_params=context->effect_is_family_start
        ? json{{"operationId",context->effect_operation},{"input",decode(context->approved_input)}}.dump() : context->approved_input;
    if (!request.authorization->allows_local_method(row->effect_method,context->effect_capability)) return {};
    context->effect_state="unused";
  }
  std::lock_guard<std::mutex> lock(mutex);
  if (closed.load() || !binding_started.load() || !page_ready || !document ||
      document->closed.load() || !document->page_scope || !document->page_scope->active() || !current) return {};
  context->document=document;
  pending.erase(std::remove_if(pending.begin(),pending.end(),[](const auto& entry){return entry.expired();}),pending.end());
  pending.push_back(context); return context;
}
gui_owner_result gui_commands_state::poll() {
  if (std::this_thread::get_id()!=owner) return gui_owner_result::wrong_owner;
  std::shared_ptr<const std::function<bool()>> proof,release;
  std::shared_ptr<const task_service::event_sink> send;
  std::shared_ptr<gui_job_context> selected;
  std::shared_ptr<gui_effect_request> selected_effect;
  std::uint64_t event_sequence=0;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (polling || effect_borrows) return closed.load()?gui_owner_result::pending:gui_owner_result::busy;
    if (closed.load()) { current.swap(release); return gui_owner_result::closed; }
    polling=true; proof=current; send=sink;
    if (!effects.empty()) { selected_effect=std::move(effects.front()); effects.erase(effects.begin()); }
    if (!selected_effect) for (const auto& item:pending) if (const auto candidate=item.lock()) {
      std::lock_guard<std::mutex> command_lock(candidate->mutex);
      if (candidate->running.load(std::memory_order_acquire) && !candidate->dispatched && !candidate->cancelled.load()) {
        selected=candidate; candidate->dispatched=true; break;
      }
    }
    if (selected && sequence<max_sequence) event_sequence=++sequence;
  }
  auto result=gui_owner_result::idle;
  try {
    if (selected_effect) {
      unsigned queued=0;
      if (closed.load() || selected_effect->context->cancelled.load() ||
          clock_type::now()>=selected_effect->deadline) {
        (void)selected_effect->phase.compare_exchange_strong(queued,3,std::memory_order_acq_rel);
        unsigned unused=0;
        (void)selected_effect->context->consumption.compare_exchange_strong(unused,2,std::memory_order_acq_rel);
      } else if (selected_effect->phase.compare_exchange_strong(queued,1,std::memory_order_acq_rel)) {
        // Only this real owner can enter effect() or invoke its native proof.
        // The waiting Bridge worker owns no GUI/task lock and is never joined here.
        auto response=effect(selected_effect->request,selected_effect->deadline);
        { std::lock_guard<std::mutex> lock(selected_effect->mutex);
          selected_effect->response=std::move(response);
          selected_effect->phase.store(2,std::memory_order_release); }
        result=gui_owner_result::dispatched;
      }
      selected_effect->changed.notify_all();
    } else if (selected) {
      if (!proof || !send || !event_sequence || closed.load() || clock_type::now()>=selected->deadline ||
          !selected->agent_scope->active() || !selected->document->page_scope->active() || !(*proof)()) {
        selected->request_cancel();
      } else {
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(selected->deadline-clock_type::now()).count();
        json payload{{"protocol","soui.gui.command/1"},{"requestId",selected->request_id},
            {"documentId",selected->document->id},{"generation",selected->document->generation},
            {"operation",selected->row->operation==gui_operation::perform?"perform":selected->row->operation==gui_operation::snapshot?"snapshot":"waitFor"},
            {"id",selected->row->id},{"action",selected->row->action},{"inputJson",selected->approved_input},
            {"effectId",selected->row->effect_method.empty()?"":"native"},{"timeoutMs",std::max<std::int64_t>(1,remaining)}};
        const auto event=json{{"sessionId",selected->document->session},{"sequence",event_sequence},
            {"eventName","gui.command"},{"payload",std::move(payload)}}.dump();
        if (event.size()>bridge_transport::max_message_bytes || closed.load() || !(*proof)() || closed.load() ||
            clock_type::now()>=selected->deadline || !selected->agent_scope->active() ||
            !selected->document->page_scope->active()) selected->request_cancel();
        else { (*send)(event); result=gui_owner_result::dispatched; }
      }
    }
  } catch (...) { close(); }
  {
    std::lock_guard<std::mutex> lock(mutex);
    polling=false;
    if (closed.load()) { current.swap(release); result=gui_owner_result::closed; }
  }
  return result;
}
std::shared_ptr<gui_job_context> gui_commands_state::find(const bridge_request& request,
    const std::string& request_id,const std::string& document_id) {
  std::lock_guard<std::mutex> lock(mutex);
  if (closed.load() || !document || document->closed.load() || !request.authorization ||
      request.authorization!=document->page_scope || !request.authorization->active()) return {};
  for (const auto& item:pending) if (const auto context=item.lock()) {
    if (context->request_id==request_id && context->document->id==document_id && !context->cancelled.load() &&
        clock_type::now()<context->deadline && context->agent_scope->active()) return context;
  }
  return {};
}
std::string gui_commands_state::page_effect(const bridge_request& request) {
  if (std::this_thread::get_id()==owner) return effect(request);
  std::shared_ptr<gui_effect_request> queued;
  try {
    const auto packet=decode(request.params_json);
    const auto context=find(request,packet.at("requestId").get<std::string>(),packet.at("documentId").get<std::string>());
    if (!context || context->row->effect_method.empty() || packet.at("effectId")!="native" ||
        decode(packet.at("inputJson").get<std::string>()).dump()!=context->approved_input)
      return reject("E_GUI_EFFECT_REJECTED");
    queued=std::make_shared<gui_effect_request>();
    queued->request=request; queued->context=context;
    queued->deadline=std::min(context->deadline,clock_type::now()+std::chrono::milliseconds(request.timeout_ms));
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (closed.load() || document!=context->document || context->cancelled.load() ||
          context->document->closed.load() || !request.authorization->active() ||
          !context->agent_scope->active() || clock_type::now()>=queued->deadline)
        return reject("E_GUI_EFFECT_REJECTED");
      if (context->effect_enqueued || context->consumption.load()!=0) return reject("E_GUI_EFFECT_USED");
      if (effects.size()>=capacity_limit) return reject("E_BUSY");
      effects.push_back(queued); context->effect_enqueued=true;
    }
    std::unique_lock<std::mutex> lock(queued->mutex);
    while (queued->phase.load(std::memory_order_acquire)<2 && !closed.load() &&
        !context->cancelled.load() && !context->document->closed.load() &&
        request.authorization->active() && context->agent_scope->active() && clock_type::now()<queued->deadline)
      queued->changed.wait_until(lock,std::min(queued->deadline,clock_type::now()+std::chrono::milliseconds(25)));
    if (queued->phase.load(std::memory_order_acquire)==2) return queued->response;
    unsigned not_started=0;
    const bool abandoned=queued->phase.compare_exchange_strong(not_started,3,std::memory_order_acq_rel);
    unsigned unused=0;
    const bool prevented=context->consumption.compare_exchange_strong(unused,2,std::memory_order_acq_rel) || unused==2;
    // A lost reply after actual owner execution starts proves neither rollback
    // nor completion. The private admission flag is never reset for a retry.
    return reject(abandoned || not_started==3 || prevented?"E_GUI_EFFECT_REJECTED":"E_GUI_EFFECT_UNKNOWN");
  } catch (...) {
    if (queued) {
      unsigned not_started=0;
      (void)queued->phase.compare_exchange_strong(not_started,3,std::memory_order_acq_rel);
      unsigned unused=0;
      const bool prevented=!queued->context || queued->context->consumption.compare_exchange_strong(unused,2,std::memory_order_acq_rel) || unused==2;
      queued->changed.notify_all();
      return reject(prevented?"E_GUI_EFFECT_REJECTED":"E_GUI_EFFECT_UNKNOWN");
    }
    return reject("E_GUI_EFFECT_REJECTED");
  }
}
std::string gui_commands_state::effect(const bridge_request& request,clock_type::time_point request_deadline) {
  if (std::this_thread::get_id()!=owner) return reject("E_GUI_WRONG_OWNER");
  const auto release=[this](gui_commands_state*) noexcept {
    std::lock_guard<std::mutex> lock(mutex); --effect_borrows;
  };
  // Scope guard only: the registry callback already holds this state alive.
  // The deleter releases a borrow and deliberately does not delete the state.
  std::unique_ptr<gui_commands_state,decltype(release)> borrow{nullptr,release};
  std::shared_ptr<gui_job_context> context;
  std::shared_ptr<const std::function<bool()>> proof;
  bool consumed=false;
  try {
    const auto packet=decode(request.params_json);
    context=find(request,packet.at("requestId").get<std::string>(),packet.at("documentId").get<std::string>());
    if (!context || context->row->effect_method.empty() || packet.at("effectId")!="native" ||
        decode(packet.at("inputJson").get<std::string>()).dump()!=context->approved_input) return reject("E_GUI_EFFECT_REJECTED");
    { std::lock_guard<std::mutex> lock(mutex);
      if (closed.load() || effect_borrows || !binding_started.load() || !page_ready ||
          document!=context->document || !current) return reject("E_GUI_EFFECT_REJECTED");
      proof=current; ++effect_borrows; borrow.reset(this); }
    const auto still_current=[&] {
      const auto authorized=[&] {
        return !closed.load() && !context->cancelled.load() && !context->document->closed.load() &&
            clock_type::now()<std::min(context->deadline,request_deadline) && request.authorization==context->document->page_scope &&
            request.authorization->active() && request.authorization->matches(request.session_id,request.origin) &&
            request.authorization->allows("gui.effect") && context->agent_scope->active() &&
            context->agent_scope->matches(context->agent_session,context->agent_origin) &&
            context->agent_scope->allows_local_method(context->row->effect_method,context->effect_capability);
      };
      if (!authorized()) return false;
      // The owner borrow retains the actual application proof, but never
      // calls it while holding GUI, task or context locks. It may close us.
      if (!(*proof)() || !authorized()) return false;
      std::lock_guard<std::mutex> lock(mutex);
      return !closed.load() && binding_started.load() && page_ready &&
          document==context->document && current==proof;
    };
    if (!still_current()) { close(); return reject("E_GUI_EFFECT_REJECTED"); }
    unsigned unused=0;
    if (!context->consumption.compare_exchange_strong(unused,1,std::memory_order_acq_rel)) return reject("E_GUI_EFFECT_USED");
    consumed=true;
    bridge_request native;
    native.request_id=context->request_id; native.session_id=context->agent_session; native.origin=context->agent_origin;
    native.method=context->row->effect_method; native.params_json=context->effect_params;
    native.operation_id=context->effect_is_family_start?context->effect_operation:std::string{};
    native.timeout_ms=static_cast<std::uint32_t>(std::max<std::int64_t>(1,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::min(context->deadline,request_deadline)-clock_type::now()).count()));
    if (!still_current() || !context->authorizes_effect(native)) {
      close(); throw std::runtime_error("GUI effect is no longer current");
    }
    const auto response=transport.handle_gui_effect(native,*context);
    const bool current_after=still_current();
    if (!current_after) close();
    {
      std::lock_guard<std::mutex> lock(context->mutex);
      // A redacted handler failure or revoked finishing scope cannot prove
      // that the application did not already apply its effect.
      context->effect_done=true; context->effect_state=response.ok && current_after?"committed":"unknown";
      if (response.ok) context->effect_result=decode(response.payload_json).dump();
    }
    if (!response.ok || !current_after) return reject("E_GUI_EFFECT_REJECTED");
    return json{{"resultJson",response.payload_json}}.dump();
  } catch (...) {
    if (borrow) close();
    if (consumed) { std::lock_guard<std::mutex> lock(context->mutex);
      context->effect_done=true; context->effect_state="unknown"; context->effect_result.clear(); }
    return reject("E_GUI_EFFECT_UNKNOWN");
  }
}
std::string gui_commands_state::ack(const bridge_request& request) {
  try {
    const auto packet=decode(request.params_json);
    const auto context=find(request,packet.at("requestId").get<std::string>(),packet.at("documentId").get<std::string>());
    if (!context) return reject("E_GUI_ACK_REJECTED");
    const auto encoded=decode(packet.at("resultJson").get<std::string>()).dump();
    const bool ok=packet.at("ok").get<bool>();
    std::lock_guard<std::mutex> lock(context->mutex);
    if (context->acknowledged || !context->dispatched || context->cancelled.load() ||
        context->document->closed.load() || !context->agent_scope->active() ||
        !context->document->page_scope->active() || clock_type::now()>=context->deadline) return reject("E_GUI_ACK_REJECTED");
    if (!context->row->effect_method.empty() && !context->effect_done) {
      unsigned unused=0;
      if (ok || (!context->consumption.compare_exchange_strong(unused,2,std::memory_order_acq_rel) && unused!=2))
        return reject("E_GUI_ACK_REJECTED");
      context->effect_state="unused"; // Negative ACK closes only an unconsumed effect; it invents no result.
    }
    context->acknowledged=true; context->ack_ok=ok;
    if (context->ack_ok) context->gui_result=encoded;
    context->changed.notify_all(); return R"({"accepted":true})";
  } catch (...) { return reject("E_GUI_ACK_REJECTED"); }
}
task_outcome gui_commands_state::run(const std::shared_ptr<gui_job_context>& context,const task_control& control) {
  context->running.store(true,std::memory_order_release);
  bool confirmed=false;
  std::string raw="{}",effect_state,effect_result;
  {
    std::unique_lock<std::mutex> lock(context->mutex);
    // Cancellation deliberately cannot lock this mutex from the task lock.
    // A bounded 25 ms recheck also covers a notify between predicate and wait.
    while (!context->acknowledged && !context->cancelled.load() && !context->document->closed.load() &&
        clock_type::now()<context->deadline)
      context->changed.wait_until(lock,std::min(context->deadline,clock_type::now()+std::chrono::milliseconds(25)));
    confirmed=context->acknowledged && context->ack_ok && !context->cancelled.load() &&
        !context->document->closed.load() && clock_type::now()<context->deadline && !control.cancellation_requested();
    raw=context->gui_result; effect_state=context->effect_state; effect_result=context->effect_result;
    if (context->consumption.load()==1 && !context->effect_done) effect_state="unknown";
  }
  context->request_cancel(); // No more effects/ACK after handler completion.
  std::string projected="{}";
  if (confirmed) try {
    projected=context->row->project_result({result_kind::success,raw,{}});
    const auto output=action_contract::parse(contract(context->row->family_name,
        decode(context->row->input_schema_json,8192),decode(context->row->result_schema_json,8192)));
    if (!output || !output->validates_output(projected) || projected.size()>32768) confirmed=false;
    else projected=decode(projected).dump();
  } catch (...) { confirmed=false; }
  if (!context->agent_scope->active() || !context->document->page_scope->active() || context->document->closed.load() ||
      clock_type::now()>=context->deadline || control.cancellation_requested()) confirmed=false;
  if (!confirmed) projected="{}";
  auto result=json{{"confirmed",confirmed},{"guiResultJson",projected},
      {"effectState",effect_state},{"effectResultJson",effect_result}}.dump();
  // Escaping two JSON strings can exceed the ordinary task result limit. Do
  // not drop the record or imply that a consumed effect was rolled back.
  if (result.size()>bridge_transport::max_params_bytes-2048) {
    confirmed=false;
    result=json{{"confirmed",false},{"guiResultJson","{}"},
        {"effectState",context->consumption.load()==1?"unknown":effect_state},{"effectResultJson",""}}.dump();
  }
  return {confirmed?result_kind::success:result_kind::failure,result,"GUI command ended."};
}
} // namespace soui::runtime::detail

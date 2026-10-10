#include <soui/diagnostics/runtime_metrics.h>
#include <soui/host/engine_thread.h>
#include <soui/host/soui_miniblink_host.h>
#include <soui/json_bridge.h>
#include <soui/runtime/services/permissions/permission_store.h>
#include <soui/runtime/services/task/task_service.h>
#include "../../../sdk/src/host/bridge_request_budget.h"
#include "../../../sdk/src/runtime/diagnostics/runtime_metrics_internal.h"
#include <json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
namespace dm=soui::sdk::diagnostics;
using namespace soui::runtime;
using namespace std::chrono_literals;
using json=nlohmann::json;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class Predicate> void await(Predicate predicate,const char* message,
    std::chrono::milliseconds duration=5000ms) {
  const auto deadline=std::chrono::steady_clock::now()+duration;
  while(!predicate()) {
    require(std::chrono::steady_clock::now()<deadline,message);
    std::this_thread::sleep_for(1ms);
  }
}
template<class Owner,class Snapshot> Snapshot read(Owner& owner) {
  Snapshot value;
  await([&] {
    const auto state=owner.read_runtime_metrics(value);
    require(state==dm::status::ready || state==dm::status::busy,"Metrics unavailable");
    return state==dm::status::ready;
  },"Metrics stayed busy");
  require(value.observation.owner_epoch!=0 && value.observation.begin_monotonic_us!=0 &&
      value.observation.end_monotonic_us>=value.observation.begin_monotonic_us &&
      !value.observation.clock_invalid,"Invalid actual sample clock/epoch");
  return value;
}
class gate final {
 public:
  void enter() {
    std::unique_lock<std::mutex> lock(mutex_); ++arrivals_; changed_.notify_all();
    require(changed_.wait_for(lock,5s,[&] { return open_; }),"Held callback timed out");
  }
  void arrivals(unsigned count) {
    std::unique_lock<std::mutex> lock(mutex_);
    require(changed_.wait_for(lock,5s,[&] { return arrivals_>=count; }),"Missing actual handler entry");
  }
  void release() noexcept { std::lock_guard<std::mutex> lock(mutex_); open_=true; changed_.notify_all(); }
 private:
  std::mutex mutex_; std::condition_variable changed_; unsigned arrivals_{0}; bool open_{false};
};
struct gate_release final { gate& value; ~gate_release() { value.release(); } };
struct join_release final {
  gate& value; std::thread& thread;
  ~join_release() { value.release(); if(thread.joinable())thread.join(); }
};
struct tasks_fixture final {
  permission_store permissions{dm::mode::scalar};
  gate held;
  std::atomic<unsigned> handler_reads{0};
  const std::string token=permissions.issue("metrics-task","app://metrics-task",
      {"task.start","task.query","task.cancel","task.subscribe","bridge.session.renew"});
  task_service service{dm::mode::scalar}; // destroyed/joined before captured owner fields
  ~tasks_fixture() { held.release(); }
  bridge_request request(const char* capability,json params) {
    bridge_request value; value.session_id="metrics-task"; value.origin="app://metrics-task";
    value.token=token; value.params_json=params.dump();
    value.authorization=permissions.resolve(value.session_id,value.origin,token,capability).scope;
    require(value.authorization!=nullptr,"Actual grant did not resolve"); return value;
  }
  std::string start(const std::string& operation,const char* kind="hold") {
    const auto reply=json::parse(service.start(request("task.start",{{"kind",kind},
        {"operationId",operation},{"input",json::object()}})));
    require(reply.contains("taskId") && reply.at("accepted")==true,"Real task start failed");
    return reply.at("taskId").get<std::string>();
  }
  json query(const std::string& id) {
    return json::parse(service.query(request("task.query",{{"taskId",id}})));
  }
  void terminal(const std::string& id) {
    await([&] { const auto result=query(id); require(!result.contains("error"),"Actual query failed");
      const auto state=result.at("state").get<std::string>();
      return state=="succeeded" || state=="failed" || state=="cancelled" || state=="partial";
    },"Real task did not become terminal");
  }
  void hold() {
    require(service.register_handler("hold",[this](const task_invocation&,const task_control&) {
      const auto actual=read<task_service,dm::tasks>(service);
      require(actual.active_handlers>=1 && actual.worker_limit==2,"Handler scalar read was not outside owner locks");
      ++handler_reads; held.enter(); return task_outcome{result_kind::success,R"({"retained":true})","done"};
    }),"Actual hold handler rejected");
  }
};

void disabled_epoch() {
  task_service disabled_task; permission_store disabled_permission; soui::sdk::engine_thread disabled_fifo;
  soui::sdk::json_bridge disabled_bridge{"app://disabled"}; soui::sdk::dynamic_miniblink_adapter disabled_host;
  dm::tasks task; dm::permissions permission; dm::queue fifo; dm::bridge_runtime bridge; dm::software_host_runtime host;
  fifo.full_rejections_total=777; host.bridge_worker_fifo.full_rejections_total=777; host.process_engine_fifo.full_rejections_total=777;
  require(disabled_task.read_runtime_metrics(task)==dm::status::disabled &&
      disabled_permission.read_runtime_metrics(permission)==dm::status::disabled &&
      !permission.has_scope_capacity_bound && permission.stored_scope_limit==0 &&
      permission.module_scope_slots_used==0 && permission.module_scope_limit==0 &&
      permission.pending_scope_reservations==0 && permission.store_scope_capacity_rejections_total==0 &&
      disabled_fifo.read_runtime_metrics(fifo)==dm::status::disabled &&
      disabled_bridge.read_runtime_metrics(bridge)==dm::status::disabled &&
      disabled_host.read_runtime_metrics(host)==dm::status::disabled,"Default metrics enabled");
  require(task.observation.owner_epoch==0 && task.worker_slots==0 && permission.bearer_alias_entries==0 &&
      fifo.posted_total==0 && fifo.full_rejections_total==0 && host.context_receipt_before==0 &&
      host.bridge_worker_fifo.full_rejections_total==0 && host.process_engine_fifo.full_rejections_total==0,
      "Disabled snapshot claimed data");
  task_service first{dm::mode::scalar},second{dm::mode::scalar};
  const auto a=read<task_service,dm::tasks>(first),b=read<task_service,dm::tasks>(second);
  require(a.observation.owner_epoch!=b.observation.owner_epoch && a.worker_slots==0 &&
      a.worker_limit==2 && a.inflight_limit==32 && a.subscription_limit==128 && a.store_limit==1024,
      "Actual limits/owner epochs changed");
  const auto again=read<task_service,dm::tasks>(first);
  require(a.observation.owner_epoch==again.observation.owner_epoch && again.committed_starts_total==0,
      "Reading mutated the owner/counters");
  static_assert(std::is_trivially_copyable_v<dm::bridge_runtime> &&
      std::is_trivially_copyable_v<dm::software_host_runtime>,"Diagnostics must remain scalar DTOs");
}
void task_capacity() {
  tasks_fixture fixture; fixture.hold();
  std::vector<std::string> ids{fixture.start("hold-0"),fixture.start("hold-1")}; fixture.held.arrivals(2);
  for(unsigned i=2;i<32;++i)ids.push_back(fixture.start("hold-"+std::to_string(i)));
  const auto duplicate=json::parse(fixture.service.start(fixture.request("task.start",
      {{"kind","hold"},{"operationId","hold-0"},{"input",json::object()}})));
  require(duplicate.at("duplicate")==true && duplicate.at("taskId")==ids[0],"Duplicate admission changed");
  const auto rejected=json::parse(fixture.service.start(fixture.request("task.start",
      {{"kind","hold"},{"operationId","overflow"},{"input",json::object()}})));
  require(rejected.value("error","")=="E_BUSY","Actual 33rd inflight task was accepted");
  auto metrics=read<task_service,dm::tasks>(fixture.service);
  require(metrics.worker_slots==2 && metrics.active_handlers==2 && metrics.inflight==32 && metrics.queued==30 &&
      metrics.inflight_high_water==32 && metrics.committed_starts_total==32 && metrics.duplicate_starts_total==1 &&
      metrics.inflight_full_rejections_total==1 && metrics.oldest_queued_age_us>0,"Task owner metrics differ from actual admission");
  require(json::parse(fixture.service.cancel(fixture.request("task.cancel",{{"taskId",ids[2]}}))).at("cancelRequested")==true,
      "Queued cancellation not requested");
  fixture.held.release();
  await([&] { metrics=read<task_service,dm::tasks>(fixture.service); return metrics.inflight==0; },"Actual workers did not settle");
  require(metrics.active_handlers==0 && metrics.queued==0 && metrics.cancelled_before_handler_total==1 &&
      metrics.finished_handlers_total==31 && metrics.terminal_entries==32 && fixture.handler_reads==31,
      "Cancellation/handler counts fabricated");
}
void task_subscriptions() {
  tasks_fixture fixture; fixture.hold(); const auto id=fixture.start("subscribed"); fixture.held.arrivals(1);
  std::vector<std::string> subscriptions;
  for(unsigned i=0;i<128;++i)subscriptions.push_back(json::parse(fixture.service.subscribe(
      fixture.request("task.subscribe",{{"taskId",id},{"eventName","task.progress"}}))).at("subscriptionId").get<std::string>());
  const auto refused=json::parse(fixture.service.subscribe(fixture.request("task.subscribe",
      {{"taskId",id},{"eventName","task.progress"}})));
  require(refused.value("error","")=="E_BUSY","Actual subscription limit changed");
  auto before=read<task_service,dm::tasks>(fixture.service);
  require(before.subscriptions==128 && before.subscription_high_water==128 && before.subscription_full_rejections_total==1,
      "Subscription counters differ");
  require(json::parse(fixture.service.unsubscribe(fixture.request("task.subscribe",
      {{"subscriptionId",subscriptions.front()}}))).at("removed")==true,"Actual unsubscribe failed");
  fixture.permissions.revoke_session("metrics-task"); fixture.service.shutdown_session("metrics-task");
  const auto after=read<task_service,dm::tasks>(fixture.service);
  require(after.subscriptions==0 && after.subscription_high_water==128,"Revoked subscription owner leaked/counter reset");
}
void task_retention() {
  tasks_fixture fixture;
  require(fixture.service.register_handler("instant",[](const task_invocation&,const task_control&) {
    return task_outcome{result_kind::success,R"({"value":7})","owned-message"};
  }),"Actual instant handler failed");
  const auto until=std::chrono::steady_clock::now()+20s; std::string first;
  for(unsigned i=0;i<1025;++i) {
    require(std::chrono::steady_clock::now()<until,"Actual retention exercise exceeded finite budget");
    const auto id=fixture.start("retained-"+std::to_string(i),"instant"); if(i==0)first=id; fixture.terminal(id);
  }
  const auto value=read<task_service,dm::tasks>(fixture.service);
  require(value.committed_starts_total==1025 && value.finished_handlers_total==1025 && value.inflight==0 &&
      value.store_entries==1024 && value.terminal_entries==1024 && value.committed_terminal_evictions_total==1 &&
      value.owner_entries==1024 && value.operation_entries==1024 && value.result_cache_entries==1024,
      "Actual bounded retention/eviction identity differs");
  require(value.store_string_size_bytes>0 && value.store_string_capacity_bytes>=value.store_string_size_bytes &&
      value.result_string_size_bytes>0 && value.result_string_capacity_bytes>=value.result_string_size_bytes,
      "Owned string lower bounds invalid");
  require(fixture.query(first).value("error","")=="E_TASK_NOT_FOUND","Actual evicted task resurrected");
}
std::atomic<std::int64_t> clock_ms{0};
permission_store::clock_type::time_point controlled_now() {
  return permission_store::clock_type::time_point{std::chrono::milliseconds(clock_ms.load())};
}
void permission_aliases() {
  clock_ms.store(0); permission_store owner{&controlled_now,dm::mode::scalar};
  const auto token=owner.issue("alias-session","app://alias",{"bridge.session.renew"});
  const auto scope=owner.resolve("alias-session","app://alias",token,"bridge.session.renew").scope;
  clock_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(25min).count());
  const auto rotated=owner.renew("alias-session","app://alias",token,"rotation"); require(rotated.ok(),"Actual rotation failed");
  const auto retry=owner.renew("alias-session","app://alias",token,"rotation");
  require(retry.ok() && retry.token==rotated.token,"Actual idempotent renewal changed");
  require(owner.renew("alias-session","app://alias",rotated.token,"early").error_code=="E_RENEW_TOO_EARLY",
      "Actual early renewal no longer refused");
  auto value=read<permission_store,dm::permissions>(owner);
  require(value.stored_scope_owners==1 && value.bearer_alias_entries==2 && value.stored_previous_aliases==1 &&
      value.issued_scopes_total==1 && value.rotated_leases_total==1 && value.idempotent_renewals_total==1 &&
      value.renewal_too_early_total==1 && value.checked_renewal_rejections_total==1 && value.has_scope_capacity_bound &&
      value.stored_scope_limit==128 && value.committed_alias_limit==256 &&
      value.renewal_alias_transient_limit==257 && value.pending_scope_reservations==0 &&
      value.module_scope_limit==512 && value.module_scope_slots_used==1,
      "Actual two aliases were counted as two scopes");
  clock_ms.fetch_add(31000);
  require(owner.check("alias-session","app://alias",token,"bridge.session.renew")!=authorization_result::authorized,
      "Old alias stayed valid beyond actual overlap");
  value=read<permission_store,dm::permissions>(owner);
  require(value.bearer_alias_entries==2 && value.stored_previous_aliases==1,"Metrics invented an expired-alias sweep");
  owner.revoke_session("alias-session"); owner.revoke_session("alias-session");
  value=read<permission_store,dm::permissions>(owner);
  require(value.stored_scope_owners==0 && value.bearer_alias_entries==0 && value.stored_previous_aliases==0 &&
      value.revoked_scopes_total==1 && scope && !scope->active(),"Alias revoke_session double-counted/revived owner");
}
void permission_owners() {
  permission_store owner{dm::mode::scalar};
  std::shared_ptr<const authorization_scope> selected;
  for(unsigned i=0;i<3;++i) {
    const auto token=owner.issue("scope-"+std::to_string(i),"app://scope",{"bridge.session.renew"});
    if(i==1)selected=owner.resolve("scope-1","app://scope",token,"bridge.session.renew").scope;
  }
  require(owner.revoke_scope(selected) && !owner.revoke_scope(selected),"Actual scope revocation differs");
  require(!owner.renew("scope-0","app://scope","unknown","no-grant").ok(),"Unknown bearer accepted");
  const auto value=read<permission_store,dm::permissions>(owner);
  require(value.stored_scope_owners==2 && value.bearer_alias_entries==2 && value.issued_scopes_total==3 &&
      value.revoked_scopes_total==1 && value.checked_renewal_rejections_total==1 && value.has_scope_capacity_bound &&
      value.stored_scope_limit==128 && value.module_scope_limit==512 && value.module_scope_slots_used==3,
      "Stored owners/capacity claim invalid");
}
void engine_fifo() {
  gate held; std::atomic<unsigned> reentered{0},executed{0};
  soui::sdk::engine_thread owner{dm::mode::scalar}; gate_release release{held};
  require(owner.start([] { return true; }),"Actual FIFO startup failed");
  require(owner.post([&] {
    const auto own=read<soui::sdk::engine_thread,dm::queue>(owner);
    require(own.executing==1,"Actual callback cannot reenter scalar read"); ++reentered; held.enter();
  }),"Actual blocking FIFO work rejected"); held.arrivals(1);
  for(unsigned i=0;i<3;++i)require(owner.post([&] { ++executed; }),"Actual FIFO post rejected");
  const auto before=read<soui::sdk::engine_thread,dm::queue>(owner);
  require(before.queued==3 && before.executing==1 && before.posted_total==4 && before.high_water>=3 &&
      before.oldest_queued_age_us>0 && before.current_callback_age_us>0 && before.has_capacity_bound && before.capacity_bound==580 &&
      before.full_rejections_total==0,
      "FIFO/native-message boundary counters invalid");
  std::thread closer([&] { owner.stop(false); }); join_release joined{held,closer};
  await([&] { return owner.state()==soui::sdk::engine_thread_state::stopping; },"Actual stop not published");
  held.release(); closer.join();
  const auto after=read<soui::sdk::engine_thread,dm::queue>(owner);
  require(after.discarded_on_stop_total==3 && after.queued==0 && after.executing==0 && reentered==1 && executed==0,
      "Actual FIFO discard/join differs");
  require(!owner.post([] {}),"Stopped FIFO accepted work");
  const auto stopped=read<soui::sdk::engine_thread,dm::queue>(owner);
  require(stopped.stopped_rejections_total==1 && stopped.full_rejections_total==0,"Stopped refusal confused with full");
}
void software_lanes() {
  using budget=soui::sdk::detail::bridge_request_budget;
  budget owner{dm::mode::scalar}; std::vector<std::shared_ptr<budget::lease>> leases; budget::rejection error{};
  const auto fill=[&](unsigned count,budget::lane lane,const char* prefix) {
    for(unsigned i=0;i<count;++i) { auto lease=owner.acquire(std::string(prefix)+std::to_string(i),1,lane,error);
      require(lease && error==budget::rejection::none,"Actual lane admission failed"); leases.push_back(std::move(lease)); }
    require(!owner.acquire(std::string(prefix)+"full",1,lane,error) && error==budget::rejection::full,"Actual lane capacity not refused");
  };
  fill(256,budget::lane::ordinary,"ordinary"); fill(128,budget::lane::control,"control"); fill(1,budget::lane::renewal,"renewal");
  require(!owner.acquire("ordinary0",1,budget::lane::ordinary,error) && error==budget::rejection::duplicate,
      "Actual outstanding identity duplicate accepted");
  const auto value=read<budget,dm::software_requests>(owner);
  require(value.ordinary.limit==256 && value.control.limit==128 && value.renewal.limit==1 &&
      value.ordinary.outstanding==256 && value.control.outstanding==128 && value.renewal.outstanding==1 &&
      value.ordinary.acquired_total==256 && value.control.acquired_total==128 && value.renewal.acquired_total==1 &&
      value.ordinary.full_rejections_total==1 && value.control.full_rejections_total==1 && value.renewal.full_rejections_total==1 &&
      value.ordinary.duplicate_rejections_total==1,"Actual lease lane accounting differs");
  leases.front()->release(); leases.front()->release(); leases.clear();
  owner.record_drop(budget::drop::queue_expired); owner.record_drop(budget::drop::stale_context);
  owner.record_drop(budget::drop::worker_post); owner.record_drop(budget::drop::engine_post);
  const auto after=read<budget,dm::software_requests>(owner);
  require(after.ordinary.outstanding==0 && after.control.outstanding==0 && after.renewal.outstanding==0 &&
      after.ordinary.released_total==256 && after.control.released_total==128 && after.renewal.released_total==1 &&
      after.queue_expired_total==1 && after.stale_context_drop_total==1 && after.worker_post_rejections_total==1 &&
      after.engine_post_rejections_total==1,"Lease release/drop counters reset or double-counted");
  std::shared_ptr<budget::lease> surviving;
  { budget temporary{dm::mode::scalar}; surviving=temporary.acquire("survivor",2,false,error); }
  require(surviving!=nullptr,"Lease failed"); surviving->release(); surviving.reset(); // actual shared owner lifetime
}
void gui_facade() {
  const auto empty=R"({"type":"object","properties":{},"required":[],"additionalProperties":false})";
  unsigned proofs=0;
  soui::sdk::json_bridge bridge{"app://metrics-gui",dm::mode::scalar};
  dm::bridge_runtime initial;
  require(bridge.read_runtime_metrics(initial)==dm::status::ready && initial.gui_commands.observation.state==dm::status::not_started,
      "Absent optional GUI poisoned facade readiness");
  gui_command_definition row{"metrics.snapshot","metrics.table","table",gui_operation::snapshot,"",
      "gui.start","gui.query","gui.cancel",empty,empty,"",[](const task_outcome&) { return "{}"; }};
  require(bridge.enable_gui_commands({std::move(row)}),"Actual GUI coordinator registration failed");
  const auto boot=json::parse(bridge.handle_json(R"({"requestId":"boot","sessionId":"","origin":"","token":"","method":"bridge.bootstrap","params":{"protocol":"soui.bridge/1"}})"));
  require(boot.at("ok")==true,"Actual bootstrap failed"); const auto credentials=boot.at("result");
  const auto bind=json::parse(bridge.handle_json(json{{"requestId","bind"},{"sessionId",credentials.at("sessionId")},
      {"origin",credentials.at("origin")},{"token",credentials.at("token")},{"method","gui.bind"},{"params",json::object()}}.dump()));
  require(bind.at("ok")==true,"Actual GUI page route bind failed");
  require(bridge.bind_gui_document("actual-native-generation",[&] { ++proofs; return true; }),"Actual GUI owner bind failed");
  const auto before=proofs; dm::bridge_runtime value;
  await([&] { return bridge.read_runtime_metrics(value)==dm::status::ready; },"Actual GUI facet busy");
  require(proofs==before && value.gui_commands.context_limit==16 && value.gui_commands.effect_queue_limit==16 &&
      value.gui_commands.context_slots_used==0 && value.gui_commands.pending_live_contexts==0 &&
      value.gui_commands.queued_effects==0 && !value.gui_commands.closed,"Metrics called actual owner proof or invented GUI work");
  bridge.close_gui_commands(); require(bridge.poll_gui_commands()==gui_owner_result::closed,"Actual GUI did not close");
  require(bridge.read_runtime_metrics(value)==dm::status::ready && value.gui_commands.closed &&
      value.task_service.committed_starts_total==0,"GUI read fabricated service tasks");
}
void software_owner() {
  soui::sdk::dynamic_miniblink_adapter owner{dm::mode::scalar}; dm::software_host_runtime sample;
  require(owner.read_runtime_metrics(sample)==dm::status::not_started &&
      sample.requests.observation.state==dm::status::not_started && sample.context_receipt_before==0,
      "Unmounted facade claimed engine readiness");
  dm::status foreign=dm::status::ready;
  std::thread other([&] { foreign=owner.read_runtime_metrics(sample); }); other.join();
  require(foreign==dm::status::wrong_owner_thread && sample.requests.observation.state==dm::status::wrong_owner_thread &&
      sample.bridge_worker_fifo.observation.state==dm::status::wrong_owner_thread &&
      sample.process_engine_fifo.observation.state==dm::status::wrong_owner_thread,"Foreign facade accessed lifecycle borrows");
}
void scalar_boundaries() {
  auto value=(std::numeric_limits<std::uint64_t>::max)()-1; bool saturated=false;
  dm::detail::add(value,2,saturated);
  require(value==(std::numeric_limits<std::uint64_t>::max)() && saturated,"Diagnostic counter wrapped");
  dm::detail::increment(value,saturated); require(value==(std::numeric_limits<std::uint64_t>::max)(),"Saturated increment wrapped");
  dm::sample sample; require(dm::detail::age(20,10,sample)==0 && sample.clock_invalid,"Reversed time fabricated age");
  require(dm::detail::combine(dm::status::busy,dm::status::unavailable)==dm::status::unavailable &&
      dm::detail::combine(dm::status::ready,dm::status::not_started)==dm::status::not_started &&
      dm::detail::combine(dm::status::unavailable,dm::status::wrong_owner_thread)==dm::status::wrong_owner_thread &&
      dm::detail::combine(dm::status::disabled,dm::status::busy)==dm::status::busy,"Facade status precedence changed");
}
struct entry { const char* name; void (*run)(); };
const entry cases[]={{"disabled-epoch",disabled_epoch},{"task-capacity",task_capacity},
    {"task-subscriptions",task_subscriptions},{"task-retention",task_retention},
    {"permission-aliases",permission_aliases},{"permission-owners",permission_owners},
    {"engine-fifo",engine_fifo},{"software-lanes",software_lanes},{"gui-facade",gui_facade},
    {"software-owner",software_owner},{"scalar-boundaries",scalar_boundaries}};
}
int main(int argc,char** argv) {
  if(argc!=2) { std::cerr<<"E_RUNTIME_METRICS_CASE\n"; return 2; }
  for(const auto& value:cases)if(value.name==std::string(argv[1])) {
    try { value.run(); std::cout<<"runtime-metrics-case "<<value.name<<" passed\n"; return 0; }
    catch(...) { std::cerr<<"E_RUNTIME_METRICS_TEST\n"; return 1; }
  }
  std::cerr<<"E_RUNTIME_METRICS_CASE\n"; return 2;
}

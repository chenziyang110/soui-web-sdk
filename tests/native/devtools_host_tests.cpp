#include <soui/host/webview_host.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

// These tests exercise the real public facade with a controlled adapter. They
// make no claim about either browser, real inspector windows or native drain.
namespace {
using namespace soui::sdk;
namespace dt = soui::sdk::devtools;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
struct resources {
  std::filesystem::path root;
  resources() {
    const auto seed=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned index=0;index!=32;++index) {
      const auto next=std::filesystem::temp_directory_path()/
          ("soui-devtools-contract-"+std::to_string(GetCurrentProcessId())+"-"+
           std::to_string(seed)+"-"+std::to_string(index));
      if(std::filesystem::create_directory(next)){root=next;break;}
    }
    require(!root.empty(),"exclusive fixture directory unavailable");
    std::ofstream output(root/"index.html",std::ios::binary);
    output<<"<!doctype html><html><body>native facade contract</body></html>";
    output.close();require(static_cast<bool>(output),"fixture write failed");
  }
  ~resources(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
  host_config config() const {
    host_config result;result.architecture=sizeof(void*)==8?"x64":"x86";
    result.resource_root=root;return result;
  }
};
struct state {
  bool mounted{},exposed{},pending{},unmount_pending{},finish_failure{};
  unsigned opens{},reloads{},unmounts{},generation{1};
  std::string document;
  std::function<void()> on_revoke;
};
class legacy_adapter : public webview_host_adapter {
 public:
  host_result mount(const host_config&) override{return {true,{},{}};}
  host_result unmount() override{return {true,{},{}};}
};
class native_adapter final : public webview_host_adapter {
 public:
  explicit native_adapter(std::shared_ptr<state> value):value_(std::move(value)){}
  host_result mount(const host_config&) override{value_->mounted=true;return {true,{},{}};}
  host_result unmount() override {
    if(value_->unmount_pending)return {false,"E_PENDING","The target still owns pending work."};
    ++value_->unmounts;value_->mounted=false;return {true,{},{}};
  }
  host_result reload() override{++value_->reloads;++value_->generation;return {true,{},{}};}
 protected:
  host_result configure_devtools_native(const dt::configuration&) override{return {true,{},{}};}
  dt::capabilities devtools_capabilities_native() const override {
    dt::capabilities result;result.native_open=true;result.detach=true;result.destroy_target=true;return result;
  }
  host_result prepare_devtools_grant(dt::target_identity& target) override {
    target=devtools_target_native();return {true,{},{}};
  }
  dt::target_identity devtools_target_native() const override {
    return {value_->generation,value_->document.empty()?"fixture-document-"+std::to_string(value_->generation):
        value_->document,value_->mounted};
  }
  host_result open_devtools_native() override {
    ++value_->opens;value_->exposed=true;return {true,{},{}};
  }
  host_result revoke_devtools_native(dt::revoke_reason) override {
    if(value_->on_revoke)value_->on_revoke();
    if(value_->pending)return {false,"E_PENDING","The controlled callback is still borrowed."};
    value_->exposed=false;return {true,{},{}};
  }
  dt::observation observe_devtools_native() const override {
    dt::observation result;result.resources_retained=value_->mounted;
    result.retirement_required=value_->exposed;return result;
  }
  host_result finish_devtools_retirement() override {
    return value_->finish_failure?host_result{false,"E_FINAL_BARRIER","Retirement failed."}:host_result{true,{},{}};
  }
 private:std::shared_ptr<state> value_;
};
dt::configuration controlled() {
  dt::configuration result;result.access=dt::policy::application_controlled;
  result.ending=dt::termination::destroy_target;result.f12=result.ctrl_shift_i=true;return result;
}
struct rescue {
  std::shared_ptr<state> value;
  ~rescue(){value->pending=value->unmount_pending=false;value->on_revoke={};}
};
void legacy_defaults(const resources& files) {
  webview_host host(std::make_unique<legacy_adapter>());
  require(!host.devtools_capabilities().native_open,"legacy capability invented");
  require(host.configure_devtools({}).ok,"legacy disabled configuration rejected");
  const auto unsupported=host.configure_devtools(controlled());
  require(!unsupported.ok&&unsupported.code=="E_NOT_SUPPORTED","legacy adapter enabled DevTools");
  require(host.start(files.config()).ok,"legacy source-compatible mount failed");
  dt::grant absent;require(!host.open_devtools(absent).accepted(),"default policy bypassed");
  require(!host.devtools_shortcut(VK_F12,false,false,false),"disabled shortcut admitted");
  require(host.destroy().ok,"legacy cleanup failed");
}
void authority_and_documents(const resources& files) {
  auto first=std::make_shared<state>(),second=std::make_shared<state>();
  webview_host a(std::make_unique<native_adapter>(first)),b(std::make_unique<native_adapter>(second));
  rescue clean_a{first},clean_b{second};
  auto invalid=controlled();invalid.ending=dt::termination::none;
  require(!a.configure_devtools(invalid).ok,"termination choice was implicit");
  require(a.configure_devtools(controlled()).ok&&b.configure_devtools(controlled()).ok,"native configuration failed");
  require(a.start(files.config()).ok&&b.start(files.config()).ok,"mount failed");
  require(a.devtools_shortcut(VK_F12,false,false,false),"native key request missing");
  require(!a.devtools_shortcut(VK_F12,false,false,false,true),"repeated key admitted");
  dt::grant absent,issued;require(!a.open_devtools(absent).accepted()&&first->opens==0,"key request granted authority");
  require(a.authorize_devtools(issued).ok,"native issuance failed");
  require(!b.open_devtools(issued).accepted()&&second->opens==0,"cross-Host grant admitted");
  host_result foreign;std::thread wrong([&]{dt::grant output;foreign=a.authorize_devtools(output);});wrong.join();
  require(!foreign.ok&&foreign.code=="E_THREAD","foreign owner issued authority");
  require(a.reload().ok,"configured but unopened target could not reload");
  require(!a.open_devtools(issued).accepted()&&first->opens==0,"old document grant admitted");
  require(a.authorize_devtools(issued).ok,"new document issuance failed");
  const auto opened=a.open_devtools(issued),again=a.open_devtools(issued);
  require(opened.accepted()&&again.id==opened.id&&first->opens==1,"same target open not deduplicated");
  const auto observed=a.observe_devtools();
  require(observed.attached==dt::truth::unknown&&observed.paused==dt::truth::unknown&&
      observed.visible==dt::truth::unknown,"accepted request invented browser observations");
  const auto reloads=first->reloads;
  const auto rejected_reload=a.reload();
  require(!rejected_reload.ok&&rejected_reload.code=="E_TARGET_RETIREMENT_REQUIRED"&&
      first->reloads==reloads,"exposed target reload reached the adapter before retirement");
  require(a.destroy().ok&&b.destroy().ok,"cleanup failed");
}
void replacement_and_native_document(const resources& files) {
  auto value=std::make_shared<state>();webview_host host(std::make_unique<native_adapter>(value));rescue clean{value};
  require(host.configure_devtools(controlled()).ok&&host.start(files.config()).ok,"mount failed");
  dt::grant original,replacement;
  require(host.authorize_devtools(original).ok&&host.authorize_devtools(replacement).ok,"replacement issuance failed");
  require(host.open_devtools(original).code=="E_UNAUTHORIZED"&&value->opens==0,
      "replacement issuance kept the old grant usable");
  // Native document changes must be rejected even without Host::reload(),
  // whose explicit grant invalidation would hide a missing receipt check.
  ++value->generation;
  require(host.open_devtools(replacement).code=="E_UNAUTHORIZED"&&value->opens==0,
      "native generation change admitted an old grant");
  dt::grant previous_document;
  require(host.authorize_devtools(previous_document).ok,"document issuance failed");
  value->document="another-native-document"; // Same numeric generation, distinct native identity.
  require(host.open_devtools(previous_document).code=="E_UNAUTHORIZED"&&!host.observe_devtools().grant_valid&&
      value->opens==0,"native document change admitted an old grant with the same generation");
  dt::grant current;
  require(host.authorize_devtools(current).ok&&host.open_devtools(current).accepted()&&value->opens==1,
      "fresh authority for the current native document was rejected");
  require(!host.open_devtools(original).accepted()&&!host.open_devtools(replacement).accepted()&&
      !host.open_devtools(previous_document).accepted()&&value->opens==1,"fresh authority revived an old grant");
  require(host.destroy().ok,"cleanup failed");
  dt::grant after_close;
  require(host.authorize_devtools(after_close).code=="E_INVALID_LIFECYCLE"&&
      host.open_devtools(current).code=="E_INVALID_LIFECYCLE"&&value->opens==1,
      "a closed Host admitted its former grant");
}
void unopened_grant_expiry(const resources& files) {
  auto value=std::make_shared<state>();webview_host host(std::make_unique<native_adapter>(value));rescue clean{value};
  require(host.configure_devtools(controlled()).ok&&host.start(files.config()).ok,"mount failed");
  dt::grant expired;
  require(host.authorize_devtools(expired,std::chrono::milliseconds{1}).ok,"finite issuance failed");
  // Do not dispatch WM_TIMER or call pump before open: admission must check
  // the real steady-clock deadline independently of a cleanup wake.
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  require(host.open_devtools(expired).code=="E_UNAUTHORIZED"&&!host.observe_devtools().grant_valid&&
      value->opens==0,"an expired grant opened before the next native timer dispatch");
  host.pump();
  require(host.session()&&!host.session()->terminal()&&value->unmounts==0,
      "unused grant expiry closed the business Host");
  dt::grant current;
  require(host.authorize_devtools(current,std::chrono::milliseconds::zero()).ok&&
      !host.open_devtools(expired).accepted()&&host.open_devtools(current).accepted()&&value->opens==1,
      "fresh authority failed to replace the expired grant");
  require(host.destroy().ok,"cleanup failed");
}
void unopened_revoke_and_reissue(const resources& files) {
  auto value=std::make_shared<state>();webview_host host(std::make_unique<native_adapter>(value));rescue clean{value};
  require(host.configure_devtools(controlled()).ok&&host.start(files.config()).ok,"mount failed");
  dt::grant revoked;
  require(host.authorize_devtools(revoked).ok,"issuance failed");
  const auto ended=host.revoke_devtools();
  require(ended.status==dt::operation_status::completed&&host.session()&&!host.session()->terminal()&&value->unmounts==0&&
      !host.observe_devtools().grant_valid,"unused grant revocation did not preserve the live business Host");
  require(host.open_devtools(revoked).code=="E_UNAUTHORIZED"&&value->opens==0,"revoked grant remained usable");
  dt::grant current;
  require(host.authorize_devtools(current).ok,"issuance after unused grant revocation failed");
  require(!host.open_devtools(revoked).accepted()&&value->opens==0,"reissuance revived the revoked grant");
  const auto opened=host.open_devtools(current);
  require(opened.accepted()&&opened.id!=ended.id&&value->opens==1,
      "reissued grant did not create its own native open operation");
  require(host.devtools_operation(ended.id).status==dt::operation_status::completed,
      "reissuance lost the completed revocation operation");
  require(host.destroy().ok,"cleanup failed");
}
void pending_retirement(const resources& files) {
  auto value=std::make_shared<state>();webview_host host(std::make_unique<native_adapter>(value));rescue clean{value};
  require(host.configure_devtools(controlled()).ok&&host.start(files.config()).ok,"mount failed");
  dt::grant issued;require(host.authorize_devtools(issued).ok,"issuance failed");
  const auto opened=host.open_devtools(issued);require(opened.accepted(),"open rejected");
  value->pending=true;
  value->on_revoke=[&]{
    const auto observed=host.observe_devtools();
    require(observed.resources_retained,"reentrant observation lost owners");
    const auto opens=value->opens;
    require(!observed.grant_valid&&!host.open_devtools(issued).accepted()&&value->opens==opens,
        "revocation callout admitted the old grant before retirement returned");
  };
  const auto pending=host.revoke_devtools();
  require(pending.status==dt::operation_status::pending&&value->unmounts==0,"pending callback abandoned");
  require(!host.open_devtools(issued).accepted()&&!host.observe_devtools().grant_valid,"revoked grant remained valid");
  value->pending=false;host.pump();
  require(host.observe_devtools().state==dt::phase::drained&&value->unmounts==1,"same retirement did not finish");
  require(host.devtools_operation(pending.id).status==dt::operation_status::completed,"operation completion lost");
  require(host.devtools_operation(opened.id).id==opened.id,"prior operation identity lost");
  require(host.destroy().ok,"destroy after completed close failed");
}
void failed_retirement(const resources& files) {
  auto value=std::make_shared<state>();webview_host host(std::make_unique<native_adapter>(value));rescue clean{value};
  require(host.configure_devtools(controlled()).ok&&host.start(files.config()).ok,"mount failed");
  dt::grant issued;require(host.authorize_devtools(issued).ok&&host.open_devtools(issued).accepted(),"open failed");
  value->finish_failure=true;const auto ended=host.revoke_devtools();
  const auto observed=host.observe_devtools();
  require(ended.status==dt::operation_status::failed&&observed.state==dt::phase::failed,
      "failed final barrier became drained");
  require(!observed.resources_retained,"physical cleanup state hidden");
  require(!host.start(files.config()).ok,"failed retirement admitted a new document");
}
void unopened_close_pending(const resources& files,dt::termination ending) {
  auto value=std::make_shared<state>();webview_host host(std::make_unique<native_adapter>(value));rescue clean{value};
  auto config=controlled();config.ending=ending;
  require(host.configure_devtools(config).ok&&host.start(files.config()).ok,"mount failed");
  // Explicit Host close must finish target retirement even without an inspector,
  // and even when the selected diagnostic ending normally supports detach.
  value->pending=true;
  const auto closing=host.close();
  require(!closing.ok&&closing.code=="E_PENDING","unopened target close did not retain the pending callback");
  const auto operation=host.observe_devtools().operation_id;
  require(operation!=0&&host.devtools_operation(operation).status==dt::operation_status::pending,
      "unopened target close has no pending operation");
  host.pump();
  const auto pending=host.observe_devtools();
  require(pending.operation_id==operation&&pending.state==dt::phase::revoking&&pending.resources_retained&&
      host.devtools_operation(operation).status==dt::operation_status::pending&&value->unmounts==0,
      "unopened target close completed before its pending callback drained");
  value->pending=false;host.pump();
  const auto finished=host.observe_devtools();
  require(finished.operation_id==operation&&finished.state==dt::phase::drained&&
      host.devtools_operation(operation).status==dt::operation_status::completed&&value->unmounts==1&&
      host.session()&&host.session()->terminal(),
      "unopened target close did not finish the original target retirement");
  require(host.destroy().ok&&value->unmounts==1,"destroy repeated a completed target unmount");
}
void detached_target_close_pending(const resources& files) {
  auto value=std::make_shared<state>();webview_host host(std::make_unique<native_adapter>(value));rescue clean{value};
  auto config=controlled();config.ending=dt::termination::detach;
  require(host.configure_devtools(config).ok&&host.start(files.config()).ok,"mount failed");
  dt::grant issued;require(host.authorize_devtools(issued).ok&&host.open_devtools(issued).accepted(),"open failed");
  const auto detached=host.revoke_devtools();
  require(detached.status==dt::operation_status::completed&&host.observe_devtools().state==dt::phase::drained&&
      value->unmounts==0&&host.session()&&!host.session()->terminal(),"detach destroyed the business target");
  value->unmount_pending=true;
  const auto closing=host.close();
  require(!closing.ok&&closing.code=="E_PENDING","detached target close did not retain pending work");
  const auto operation=host.observe_devtools().operation_id;
  require(operation!=0&&operation!=detached.id,"target close reused the completed detach operation");
  host.pump();
  const auto pending=host.observe_devtools();
  require(pending.operation_id==operation&&pending.state==dt::phase::revoking&&pending.resources_retained&&
      host.devtools_operation(operation).status==dt::operation_status::pending&&value->unmounts==0,
      "completed detach hid a pending target close");
  value->unmount_pending=false;host.pump();
  const auto finished=host.observe_devtools();
  require(finished.operation_id==operation&&finished.state==dt::phase::drained&&
      host.devtools_operation(operation).status==dt::operation_status::completed&&value->unmounts==1&&
      host.session()&&host.session()->terminal(),"detached target close did not finish its original operation");
  require(host.destroy().ok&&value->unmounts==1,"destroy repeated a detached target unmount");
}
}
int main() {
  try {resources files;legacy_defaults(files);authority_and_documents(files);
    replacement_and_native_document(files);unopened_grant_expiry(files);unopened_revoke_and_reissue(files);
    pending_retirement(files);failed_retirement(files);
    unopened_close_pending(files,dt::termination::destroy_target);
    unopened_close_pending(files,dt::termination::detach);
    detached_target_close_pending(files);
    std::cout<<"Public DevTools facade contract: 10 scenarios passed; backend behavior untested.\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

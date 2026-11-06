#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <json.hpp>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace perf {
using json=nlohmann::json;
namespace fs=std::filesystem;
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Publish a borrowed adapter only after its owning Host has been constructed.
// The factory may throw after taking ownership; neither destination then changes.
template<class Owner,class Adapter,class Factory>
void adopt_adapter(std::unique_ptr<Owner>& owner,Adapter*& alias,std::unique_ptr<Adapter> adapter,Factory&& factory){
  require(!owner&&!alias,"Adapter owner must be empty");auto* borrowed=adapter.get();
  auto constructed=factory(std::move(adapter));require(bool(constructed),"Adapter owner construction returned null");
  owner=std::move(constructed);alias=borrowed;
}
class dispatch_gate final {
 public:
  struct receipt final {const dispatch_gate* owner{};std::uint64_t epoch{};std::string session;};
  class scope final {
   public:
    explicit scope(const dispatch_gate& owner)noexcept:owner_(owner),epoch_(owner.epoch()),previous_(current_){current_=this;}
    ~scope(){current_=previous_;}
    scope(const scope&)=delete;scope& operator=(const scope&)=delete;
   private:
    friend class dispatch_gate;
    const dispatch_gate& owner_;const std::uint64_t epoch_;scope* previous_;
    inline static thread_local scope* current_{};
  };
  class revocation final {
   public:
    explicit revocation(dispatch_gate& gate):gate_(gate),counted_(gate.begin_revocation()){}
    ~revocation(){if(!finished_)gate_.end_revocation(counted_,false);}
    void complete(){if(!finished_){gate_.end_revocation(counted_,true);finished_=true;}}
    revocation(const revocation&)=delete;revocation& operator=(const revocation&)=delete;
   private:dispatch_gate& gate_;bool counted_{},finished_{};
  };
  receipt original(const std::string& parsed_request_session)const{
    // The SDK contextual request owns the original parsed session; never use a
    // later current-session read or reparse a large echo envelope here.
    require(!parsed_request_session.empty(),"E_DISPATCH_SESSION");
    for(auto* frame=scope::current_;frame;frame=frame->previous_)
      if(&frame->owner_==this)return {this,frame->epoch_,parsed_request_session};
    throw std::runtime_error("E_DISPATCH_SCOPE");
  }
  std::uint64_t epoch()const noexcept{return epoch_.load(std::memory_order_acquire);}
  void revoke(){revocation pending(*this);pending.complete();}
  template<class Publish>bool publish(const receipt& value,Publish&& publish){
    std::lock_guard<std::mutex> lock(mutex_);if(!matches(value))return false;publish();return true;
  }
  // Claim is the UI execution linearization point, not a lock held across a
  // reentrant Host/COM call. A later revocation cannot relabel this receipt.
  bool claim(const receipt& value){std::lock_guard<std::mutex> lock(mutex_);return matches(value);}
 private:
  bool begin_revocation(){
    std::lock_guard<std::mutex> lock(mutex_);
    // One atomic admission value: a request cannot observe a new epoch while
    // inner permissions still authorize the old session. Zero never publishes.
    epoch_.store(0,std::memory_order_release);
    if(generation_==UINT64_MAX)failed_closed_=true;else ++generation_;
    if(revocations_==UINT64_MAX){failed_closed_=true;return false;}
    ++revocations_;return true;
  }
  void end_revocation(bool counted,bool succeeded){
    std::lock_guard<std::mutex> lock(mutex_);
    if(!succeeded)failed_closed_=true;
    if(counted){if(revocations_)--revocations_;else failed_closed_=true;}
    if(!revocations_&&!failed_closed_)epoch_.store(generation_,std::memory_order_release);
  }
  bool matches(const receipt& value)const noexcept{return value.owner==this&&value.epoch&&value.epoch==epoch()&&!value.session.empty();}
  std::mutex mutex_;std::atomic<std::uint64_t> epoch_{1};
  std::uint64_t generation_{1},revocations_{};bool failed_closed_{};
};
struct handle final {
  HANDLE value{};
  explicit handle(HANDLE item=nullptr):value(item){}
  ~handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
  handle(const handle&)=delete;handle& operator=(const handle&)=delete;
};
inline std::string sha256(const fs::path& path){
  BCRYPT_ALG_HANDLE provider{};BCRYPT_HASH_HANDLE hash{};
  require(BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA provider failed");
  struct provider_owner {BCRYPT_ALG_HANDLE value;~provider_owner(){BCryptCloseAlgorithmProvider(value,0);}} owned_provider{provider};
  require(BCryptCreateHash(provider,&hash,nullptr,0,nullptr,0,0)>=0,"SHA hash failed");
  struct hash_owner {BCRYPT_HASH_HANDLE value;~hash_owner(){BCryptDestroyHash(value);}} owned_hash{hash};
  std::ifstream file(path,std::ios::binary);require(bool(file),"Cannot open hash input");std::array<char,65536> buffer{};
  while(file){file.read(buffer.data(),buffer.size());const auto count=file.gcount();
    if(count)require(BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(count),0)>=0,"SHA update failed");}
  require(file.eof(),"Hash read failed");std::array<unsigned char,32> bytes{};
  require(BCryptFinishHash(hash,bytes.data(),static_cast<ULONG>(bytes.size()),0)>=0,"SHA finish failed");
  const char* hex="0123456789abcdef";std::string result;result.reserve(64);
  for(auto byte:bytes){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
inline void write_new(const fs::path& path,const std::string& text){
  require(text.size()<=128*1024,"Native report exceeds 128 KiB");
  handle file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
  require(file.value!=INVALID_HANDLE_VALUE,"Refuse missing/unwritable or existing report");DWORD count{};
  require(WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)&&count==text.size()&&FlushFileBuffers(file.value),"Report write/flush failed");
}
inline json parse_object(std::string_view text){
  require(text.size()<=65536,"E_BENCHMARK_PARAMS_SIZE");auto value=json::parse(text);
  require(value.is_object(),"E_BENCHMARK_PARAMS_TYPE");return value;
}
inline json echo(std::string_view params){
  const auto value=parse_object(params);require(value.size()==2&&value.contains("payload")&&value["payload"].is_string()&&
    value.contains("sequence")&&value["sequence"].is_number_unsigned(),"E_BENCHMARK_ECHO_PARAMS");
  const auto& payload=value["payload"].get_ref<const std::string&>();
  require(payload.size()<=60000&&value["sequence"].get<std::uint64_t>()<=UINT32_MAX,"E_BENCHMARK_ECHO_LIMIT");
  for(unsigned char byte:payload)require(byte>=32&&byte<=126,"E_BENCHMARK_ECHO_ASCII");return value;
}
class report_writer final {
 public:
  struct prepared_report final {std::string text;};
  explicit report_writer(fs::path root):root_(std::move(root)),thread_([this]{run();}){}
  ~report_writer(){stop();if(!stopped_.load())std::terminate();if(thread_.joinable())thread_.join();}
  static prepared_report prepare(json value){
    auto text=value.dump(2)+"\n";require(text.size()<=128*1024,"E_REPORT_SIZE");
    return {std::move(text)};
  }
  json enqueue(json value){return enqueue_prepared(prepare(std::move(value)));}
  json enqueue_prepared(prepared_report prepared){
    require(prepared.text.size()<=128*1024,"E_REPORT_SIZE");
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_||queue_.size()>=4||status_.size()>=64)return {{"accepted",false},{"error","E_REPORT_CAPACITY"}};
    const auto id=++next_;const auto path=root_/("report-"+std::to_string(id)+".json");
    json receipt={{"accepted",true},{"path",path.u8string()},{"id",id}};
    status_.emplace(id,json{{"state","pending"},{"path",path.u8string()},{"id",id}});
    try{queue_.push_back({id,path,std::move(prepared.text)});}catch(...){status_.erase(id);throw;}
    changed_.notify_one();return receipt;
  }
  json status(unsigned id)const{
    std::lock_guard<std::mutex> lock(mutex_);const auto found=status_.find(id);
    return found==status_.end()?json{{"state","failed"},{"error","E_REPORT_UNKNOWN"}}:found->second;
  }
  void stop(){std::lock_guard<std::mutex> lock(mutex_);stopping_=true;changed_.notify_one();}
  bool stopped()const noexcept{return stopped_.load(std::memory_order_acquire);}
 private:
  struct entry {unsigned id;fs::path path;std::string text;};
  void run()noexcept{
    try{
      for(;;){entry task;{
          std::unique_lock<std::mutex> lock(mutex_);changed_.wait(lock,[this]{return stopping_||!queue_.empty();});
          if(queue_.empty())break;task=std::move(queue_.front());queue_.pop_front();}
        std::string error;try{write_new(task.path,task.text);}catch(const std::exception& failure){error=failure.what();}
        {std::lock_guard<std::mutex> lock(mutex_);auto& state=status_.at(task.id);state["state"]=error.empty()?"saved":"failed";if(!error.empty())state["error"]=error;}
      }
    }catch(...){failed_.store(true);}
    stopped_.store(true,std::memory_order_release);
  }
 public:
  bool failed()const noexcept{return failed_.load();}
 private:
  fs::path root_;mutable std::mutex mutex_;std::condition_variable changed_;std::deque<entry> queue_;std::map<unsigned,json> status_;
  unsigned next_{};bool stopping_{};std::atomic<bool> stopped_{false},failed_{false};std::thread thread_;
};
}

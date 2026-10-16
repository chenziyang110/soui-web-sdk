#include "../acceptance-app/process-address-census.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace soui::acceptance::address_census;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
struct fixture final {
  enum class mode { normal, zero, gap, unknown_state, unknown_type, short_query, error, clock_failure, clock_backwards, slow, region_limit } selected{mode::normal};
  std::uint64_t tick{100}; unsigned clocks{};
} current;
BOOL WINAPI counter(LARGE_INTEGER* value) {
  ++current.clocks;
  if(current.selected==fixture::mode::clock_failure && current.clocks==3)return FALSE;
  value->QuadPart=static_cast<LONGLONG>(current.tick);
  if(current.selected==fixture::mode::clock_backwards && current.clocks==3)value->QuadPart=99;
  return TRUE;
}
SIZE_T WINAPI query(LPCVOID address,PMEMORY_BASIC_INFORMATION value,SIZE_T length) {
  if(length!=sizeof(*value))return 0;
  const auto number=reinterpret_cast<std::uintptr_t>(address);
  value->BaseAddress=const_cast<void*>(address);value->RegionSize=4096;
  value->State=MEM_COMMIT;value->Type=MEM_PRIVATE;
  if(current.selected==fixture::mode::normal) {
    if(number==8192)value->Type=MEM_MAPPED;
    if(number==12288)value->Type=MEM_IMAGE;
    if(number==16384)value->State=MEM_RESERVE;
    if(number==20480) { value->State=MEM_FREE; value->Type=0xdead;value->RegionSize=8192; }
  }
  if(current.selected==fixture::mode::zero)value->RegionSize=0;
  if(current.selected==fixture::mode::gap)value->BaseAddress=reinterpret_cast<void*>(number+4096);
  if(current.selected==fixture::mode::unknown_state)value->State=0xdead;
  if(current.selected==fixture::mode::unknown_type)value->Type=0xdead;
  if(current.selected==fixture::mode::short_query)return sizeof(*value)-1;
  if(current.selected==fixture::mode::error) { SetLastError(ERROR_ACCESS_DENIED);return 0; }
  if(current.selected==fixture::mode::slow)current.tick+=51;
  return sizeof(*value);
}
result run(fixture::mode mode=fixture::mode::normal,std::uintptr_t maximum=24575) {
  current={};current.selected=mode;return detail::collect(4096,maximum,1000,{query,counter});
}
void contracts() {
  const auto value=run();require(std::string(value.state) == "complete","complete range");
  require(value.queries==5 && value.next_address==24576,"complete and clipped final region");
  require(value.private_bytes==4096 && value.mapped_bytes==4096 && value.image_bytes==4096 &&
    value.reserved_bytes==4096 && value.free_bytes==4096,"disjoint exact states and types");
  const auto encoded=serialize(value);require(encoded.at("coveredBytes")=="20480" &&
    encoded.at("committedTotalBytes")=="12288" && encoded.at("nonAtomic")==true && encoded.at("physicalMemory")==false,
    "serialized scope and exact totals");
  for(const auto& selected:std::array<std::pair<fixture::mode,const char*>,9>{{
    {fixture::mode::zero,"invalid-region"},{fixture::mode::gap,"invalid-region"},
    {fixture::mode::unknown_state,"unknown-state"},{fixture::mode::unknown_type,"unknown-type"},
    {fixture::mode::short_query,"query-error"},{fixture::mode::error,"query-error"},
    {fixture::mode::clock_failure,"clock-error"},{fixture::mode::clock_backwards,"clock-error"},
    {fixture::mode::slow,"time-budget"}}}) {
    const auto failed=run(selected.first);require(std::string(failed.state)==selected.second,"known failure retained");
    require(failed.next_address==4096 && failed.private_bytes==0,"failed query never advances coverage");
    if(selected.first==fixture::mode::error)require(failed.error==ERROR_ACCESS_DENIED,"original query error");
  }
  const auto many=run(fixture::mode::region_limit,static_cast<std::uintptr_t>(maximum_queries+2)*4096-1);
  require(std::string(many.state)=="region-budget" && many.queries==maximum_queries,"bounded number of queries");
  require(many.next_address==static_cast<std::uint64_t>(maximum_queries+1)*4096,"partial range remains explicit");
  require(std::string(detail::collect(0,4095,1000,{query,counter}).state)=="invalid-range","invalid lower bound");
  require(std::string(detail::collect(4096,2048,1000,{query,counter}).state)=="invalid-range","reversed range");
  require(std::string(detail::collect(4096,8191,0,{query,counter}).state)=="invalid-range","zero frequency");
  std::cout<<"Bounded address-space contract groups passed\n";
}
void actual() {
  void* private_data=VirtualAlloc(nullptr,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
  require(private_data!=nullptr,"real private allocation");
  struct private_owner { void* value; ~private_owner(){if(value)VirtualFree(value,0,MEM_RELEASE);} } own_private{private_data};
  HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,65536,nullptr);
  require(mapping!=nullptr,"real mapped section");
  struct handle_owner { HANDLE value; ~handle_owner(){if(value)CloseHandle(value);} } own_mapping{mapping};
  void* view=MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,65536);
  require(view!=nullptr,"real mapped view");
  struct view_owner { void* value; ~view_owner(){if(value)UnmapViewOfFile(value);} } own_view{view};
  MEMORY_BASIC_INFORMATION a{},b{};
  require(VirtualQuery(private_data,&a,sizeof(a))==sizeof(a) && a.State==MEM_COMMIT && a.Type==MEM_PRIVATE,"real private metadata");
  require(VirtualQuery(view,&b,sizeof(b))==sizeof(b) && b.State==MEM_COMMIT && b.Type==MEM_MAPPED,"real mapped metadata");
  const auto value=capture();
  std::cout<<serialize(value).dump()<<'\n';
  require(std::string(value.state)=="complete","actual address-space scan must complete within original bounds");
  require(value.private_bytes>=65536 && value.mapped_bytes>=65536 && value.image_bytes>0,"actual three committed types observed");
  require(value.next_address==value.maximum+1 && value.end_qpc>=value.begin_qpc,"actual complete range and time");
  const auto covered=value.private_bytes+value.mapped_bytes+value.image_bytes+value.reserved_bytes+value.free_bytes;
  require(covered==value.maximum-value.minimum+1,"actual no missing or overlapping address range");
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"select contracts or actual");
    const std::string selected=argv[1];
    if(selected=="contracts")contracts();else if(selected=="actual")actual();else throw std::runtime_error("unknown case");
    return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}

#include "../native_input_policy.h"
#include <cstdio>
#include <memory>
#include <stdexcept>
using namespace owner_fairness;
namespace {
void need(bool b){if(!b)throw std::runtime_error("contract failed");}
input_row row(unsigned i,std::uint64_t q){input_row r{};r.receipt=q;r.retrieved=q-1;r.extra=tag_magic|i;r.tag=i;r.x=40+16*static_cast<int>((i-1)%64);r.y=63;r.dispatch_tuple=true;r.thread=7;r.stage=2;return r;}
void all(input_policy&p){for(unsigned i=1;i<=input_count;++i)p.record(row(i,100+i));}
template<class F>void test(const char*name,F f){f();std::printf("PASS %s\n",name);}
}
int main(){try{
 test("complete3125-and-front-tail-gaps",[]{auto p=std::make_unique<input_policy>();need(p->arm(100));all(*p);p->close(4000);need(p->complete()&&p->stored==3125&&p->max_gap==775);});
 test("untagged-during-armed-window-is-not-filtered",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.tag=0;r.extra=0;p->record(r);need(p->failed&&p->first_fault==5&&p->stored==1);});
 test("missing-first-tag-rejects-order",[]{auto p=std::make_unique<input_policy>();p->arm(100);p->record(row(2,101));need(p->first_fault==6&&!p->complete());});
 test("duplicate-tag-rejects-order",[]{auto p=std::make_unique<input_policy>();p->arm(100);p->record(row(1,101));p->record(row(1,102));need(p->first_fault==6);});
 test("wrong-native-cell-not-nearest",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.x=48;p->record(r);need(p->first_fault==7);});
 test("extra-info-full-value-required",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.extra|=1ULL<<40;p->record(r);need(p->first_fault==5);});
 test("real-retrieved-dispatch-tuple-required",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.dispatch_tuple=false;p->record(r);need(p->first_fault==4);});
 test("QPC-backward-rejected",[]{auto p=std::make_unique<input_policy>();p->arm(100);p->record(row(1,99));need(p->first_fault==3);});
 test("arm-cannot-reset-after-start",[]{auto p=std::make_unique<input_policy>();p->arm(100);need(!p->arm(200)&&p->first_fault==1);});
 test("finish-attempt-is-not-success",[]{auto p=std::make_unique<input_policy>();p->arm(100);p->record(row(1,101));p->close(102);need(p->closed&&!p->complete()&&p->first_fault==9);});
 test("raw-outside-window-preserved",[]{auto p=std::make_unique<input_policy>();auto r=row(1,90);r.tag=0;r.extra=0;p->record(r);p->arm(100);all(*p);p->close(4000);r.receipt=4001;p->record(r);need(p->complete()&&p->stored==3127&&!p->rows[0].in_cohort&&!p->rows[3126].in_cohort);});
 test("overflow-never-passes-truncated-ledger",[]{auto p=std::make_unique<input_policy>();for(unsigned i=0;i<native_capacity+1;++i)p->record(row(1,100+i));need(p->overflow&&p->failed&&p->stored==native_capacity&&p->seen==native_capacity+1&&!p->complete());});
 test("empty-progress-retains-whole-tail",[]{progress p;p.start(100);need(p.finish(500)&&p.count==0&&p.max_gap==400);});
 test("progress-clock-and-head-tail",[]{progress p;p.start(100);need(p.note(120)&&p.note(140)&&p.finish(200)&&p.max_gap==60&&!p.note(139));});
 std::puts("Contracts:14 passed; model boundary only, no Windows/SDK/IME/physical qualification");return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}

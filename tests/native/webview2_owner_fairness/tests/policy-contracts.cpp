#include "../native_input_policy.h"
#include <cstdio>
#include <memory>
#include <stdexcept>
using namespace owner_fairness;
namespace {
void need(bool b){if(!b)throw std::runtime_error("contract failed");}
input_row row(unsigned i,std::uint64_t q){input_row r{};r.receipt=q;r.retrieved=q-1;r.extra=tag_magic|i;r.tag=i;r.x=40+16*static_cast<int>((i-1)%64);r.y=63;r.dispatch_tuple=true;r.thread=7;r.stage=2;r.retrieval_known=true;r.retrieval_ordinal=i;r.retrieval_extra=r.entry_extra=r.extra;return r;}
void all(input_policy&p){for(unsigned i=1;i<=input_count;++i)p.record(row(i,100+i));}
void seal(input_policy&p){p.observe_sender_done(3999);need(p.seal_on_owner(4000));p.observe_sender_terminal(4000);}
unsigned tests{};
template<class F>void test(const char*name,F f){f();++tests;std::printf("PASS %s\n",name);}
}
int main(){try{
 test("complete3125-and-front-tail-gaps",[]{auto p=std::make_unique<input_policy>();need(p->arm(100));all(*p);seal(*p);need(p->complete()&&p->stored==3125&&p->max_gap==775);});
 test("untagged-during-armed-window-is-not-filtered",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.tag=0;r.extra=r.retrieval_extra=r.entry_extra=0;p->record(r);need(p->failed&&p->first_fault==5&&p->stored==1);});
 test("missing-first-tag-rejects-order",[]{auto p=std::make_unique<input_policy>();p->arm(100);p->record(row(2,101));need(p->first_fault==6&&!p->complete());});
 test("duplicate-tag-rejects-order",[]{auto p=std::make_unique<input_policy>();p->arm(100);p->record(row(1,101));p->record(row(1,102));need(p->first_fault==6);});
 test("wrong-native-cell-not-nearest",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.x=48;p->record(r);need(p->first_fault==7);});
 test("extra-info-full-value-required",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.extra|=1ULL<<40;r.retrieval_extra=r.entry_extra=r.extra;p->record(r);need(p->first_fault==5);});
 test("real-retrieved-dispatch-tuple-required",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.dispatch_tuple=false;p->record(r);need(p->first_fault==4);});
 test("QPC-backward-rejected",[]{auto p=std::make_unique<input_policy>();p->arm(100);p->record(row(1,99));need(p->first_fault==3);});
 test("arm-cannot-reset-after-start",[]{auto p=std::make_unique<input_policy>();p->arm(100);need(!p->arm(200)&&p->first_fault==1);});
 test("sender-done-alone-does-not-close",[]{auto p=std::make_unique<input_policy>();p->arm(100);p->record(row(1,101));p->observe_sender_done(102);need(!p->seal_on_owner(103)&&!p->closed&&!p->complete());});
 test("raw-outside-window-preserved",[]{auto p=std::make_unique<input_policy>();auto r=row(1,90);r.tag=0;r.extra=0;p->record(r);p->arm(100);all(*p);seal(*p);r.receipt=4001;p->record(r);need(p->complete()&&p->stored==3127&&!p->rows[0].in_cohort&&!p->rows[3126].in_cohort);});
 test("overflow-never-passes-truncated-ledger",[]{auto p=std::make_unique<input_policy>();for(unsigned i=0;i<native_capacity+1;++i)p->record(row(1,100+i));need(p->overflow&&p->failed&&p->stored==native_capacity&&p->seen==native_capacity+1&&!p->complete());});
 test("empty-progress-retains-whole-tail",[]{progress p;p.start(100);need(p.finish(500)&&p.count==0&&p.max_gap==400);});
 test("progress-clock-and-head-tail",[]{progress p;p.start(100);need(p.note(120)&&p.note(140)&&p.finish(200)&&p.max_gap==60&&!p.note(139));});
 test("all-tags-before-sender-notification-need-owner-opportunity",[]{auto p=std::make_unique<input_policy>();p->arm(100);all(*p);need(!p->seal_on_owner(3900)&&!p->closed);p->observe_sender_done(3999);need(!p->closed);need(p->seal_on_owner(4000)&&!p->complete());p->observe_sender_terminal(4001);need(p->complete());});
 test("sender-done-before-tail-retains-tail-in-cohort",[]{auto p=std::make_unique<input_policy>();p->arm(100);for(unsigned i=1;i<=2725;++i)p->record(row(i,100+i));p->observe_sender_done(2826);need(!p->seal_on_owner(2827));for(unsigned i=2726;i<=input_count;++i)p->record(row(i,100+i));need(p->rows[3124].in_cohort);need(p->seal_on_owner(4000));p->observe_sender_terminal(4001);need(p->complete());});
 test("tag0-fault-does-not-hide-complete-positive-sequence",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto z=row(1,101);z.tag=0;z.extra=z.retrieval_extra=z.entry_extra=0;p->record(z);for(unsigned i=1;i<=input_count;++i)p->record(row(i,101+i));seal(*p);need(p->positive_sequence_complete()&&p->closed&&p->failed&&p->first_fault==5&&!p->complete()&&p->stored==3126);});
 test("wrong-cell-fault-keeps-raw-positive-progress",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.x=48;p->record(r);for(unsigned i=2;i<=input_count;++i)p->record(row(i,100+i));seal(*p);need(p->positive_sequence_complete()&&p->first_fault==7&&!p->complete());});
 test("identity-snapshot-disagreement-remains-failure",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.entry_extra=0;p->record(r);for(unsigned i=2;i<=input_count;++i)p->record(row(i,100+i));seal(*p);need(p->positive_sequence_complete()&&p->first_fault==4&&!p->complete());});
 test("late-stage-cannot-be-repaired-by-full-tail",[]{auto p=std::make_unique<input_policy>();p->arm(100);for(unsigned i=1;i<=input_count;++i){auto r=row(i,100+i);if(i==input_count)r.stage=3;p->record(r);}seal(*p);need(p->positive_sequence_complete()&&p->first_fault==10&&!p->complete());});
 test("system-source-diagnostic-does-not-waive-tag0",[]{auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.tag=0;r.extra=r.retrieval_extra=r.entry_extra=0;r.source_attempted=r.source_ok=true;r.source_origin=4;p->record(r);need(p->first_fault==5&&p->failed&&!p->complete());});
 test("receiver-drain-does-not-hide-pre-terminal-tag0",[]{auto p=std::make_unique<input_policy>();p->arm(100);all(*p);p->observe_sender_done(3999);need(p->seal_on_owner(4000));auto z=row(1,4001);z.tag=0;z.extra=z.retrieval_extra=z.entry_extra=0;p->record(z);p->observe_sender_terminal(4002);need(!p->rows[3125].in_cohort&&p->rows[3125].identity_window&&p->first_fault==5&&!p->complete());});
 test("nonzero-sent-flags-never-qualify-claimed-tuple",[]{const unsigned sent_kinds[]{1u,2u,4u,8u};for(const unsigned flags:sent_kinds){auto p=std::make_unique<input_policy>();p->arm(100);auto r=row(1,101);r.sent_flags=flags;p->record(r);for(unsigned i=2;i<=input_count;++i)p->record(row(i,100+i));seal(*p);need(p->rows[0].sent_flags==flags&&p->stored==input_count&&p->positive_sequence_complete()&&p->first_fault==14&&!p->complete());}});
 std::printf("Contracts:%u passed; model boundary only, no Windows/SDK/IME/physical qualification\n",tests);return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}

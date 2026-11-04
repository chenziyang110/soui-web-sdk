#include "benchmarks/render-phase-cost/RenderPhaseCost.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace phase = soui::bench::render_phase;
using phase::u32;
using phase::u64;
thread_local u32 actual_thread = 11;
struct fake_clock {
    std::atomic<u64> value{100}, calls{0};
    u64 now_qpc() noexcept { calls.fetch_add(1); return value.load(); }
    u32 current_thread_id() noexcept { return actual_thread; }
};
using bank = phase::bank<fake_clock>;
using scope = bank::scope;
static_assert(!std::is_copy_constructible<scope>::value && !std::is_move_constructible<scope>::value,
    "A scope stays at its actual producer call site, including C++14 builds");
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
phase::binding session() { phase::binding b; b.process_id=17; b.process_creation=19; b.run_low=23; b.qpc_frequency=10000000; return b; }
void start(bank& b) { actual_thread=11; require(b.configure(true,session()),"cold configure/lock-free support"); require(b.register_producer(0,11),"actual slot register"); }
phase::snapshot finish(bank& b) { require(b.release_producer(0),"release producer"); b.stop(); return b.final_snapshot(true); }
const phase::metric_snapshot& raster(const phase::snapshot& s) { return s.metrics[static_cast<std::size_t>(phase::metric::raster_qpc)]; }
void unavailable(const phase::snapshot& s, phase::status expected) { require(s.state==expected&&!s.histograms_available,"busy/fault/disabled has no usable histogram"); }

void off_and_unregistered_do_not_read_clocks() {
    fake_clock c; bank b(c); unsigned mono=0;
    { scope s(b,0,phase::metric::raster_qpc); require(!s,"default off scope"); }
    b.observe_lateness(0,phase::metric::task_due_late_us,1.0,[&]{++mono;return 2.0;});
    require(!b.register_producer(0,11),"off registration");
    unavailable(b.final_snapshot(true),phase::status::disabled);
    require(b.configure(false,session()),"explicit off");
    require(!b.configure(true,session()),"off cannot be reset into recording");
    require(c.calls==0&&mono==0,"off must not read either clock");
    fake_clock d; bank enabled(d); require(enabled.configure(true,session()),"enabled cold");
    { scope s(enabled,0,phase::metric::raster_qpc); require(!s,"unregistered scope"); }
    enabled.observe_lateness(0,phase::metric::shared_wake_late_us,1.0,[&]{++mono;return 2.0;});
    require(d.calls==0&&mono==0,"unregistered must not read clocks");
}
void nested_scopes_are_independent_inclusive_wall() {
    fake_clock c; bank b(c); start(b);
    scope outer(b,0,phase::metric::raster_qpc); c.value=103;
    { scope copy(b,0,phase::metric::spare_copy_qpc); c.value=107; require(copy.finish(),"copy returned"); }
    c.value=110; require(outer.finish(),"raster returned");
    auto s=finish(b); require(s.histograms_available,"exact final");
    require(raster(s).observations==1&&raster(s).total_units==10,"outer includes nested work");
    require(s.metrics[3].total_units==4,"copy has its own inclusive duration; no subtraction");
    require(c.calls==4,"one begin/end per admitted scope");
}
void logarithmic_boundaries_and_highest_bucket() {
    fake_clock c; bank b(c); start(b);
    const u64 values[]={0,1,2,3,4,7,8,u64{1}<<63};
    u64 tick=100;
    for (u64 delta:values) { c.value=tick; scope s(b,0,phase::metric::raster_qpc); tick+=delta; c.value=tick; require(s.finish(),"boundary observation"); }
    auto s=finish(b); const auto& m=raster(s);
    require(m.observations==8&&m.buckets[0]==2&&m.buckets[1]==2&&m.buckets[2]==2&&m.buckets[3]==1&&m.buckets[63]==1,"0/1 share bucket zero; 2^63 uses bucket63");
    require(m.total_units==(u64{1}<<63)+25,"exact units without quantile invention");
}
void resume_and_recycled_thread_id_keep_slot_history() {
    fake_clock c; bank b(c); start(b);
    { scope s(b,0,phase::metric::raster_qpc); c.value=101; s.finish(); }
    require(b.release_producer(0)&&b.register_producer(0,11),"same actual slot resumes");
    { scope s(b,0,phase::metric::raster_qpc); c.value=102; s.finish(); }
    require(b.release_producer(0)&&b.register_producer(1,11),"new trace lifetime slot may reuse inactive Windows tid");
    { scope s(b,1,phase::metric::raster_qpc); c.value=103; s.finish(); }
    require(b.release_producer(1),"release second lifetime slot"); b.stop(); auto s=b.final_snapshot(true);
    require(s.histograms_available&&s.registered_producers==2&&raster(s).observations==3,"resume does not clear history");
}
void all_thirty_two_trace_slots_aggregate() {
    fake_clock c; bank b(c); require(b.configure(true,session()),"configure");
    for (std::size_t i=0;i<phase::producer_limit;++i) {
        actual_thread=static_cast<u32>(i+1); require(b.register_producer(i,actual_thread),"fixed slot admission");
        scope s(b,i,phase::metric::raster_qpc); c.value.fetch_add(1); require(s.finish(),"slot sample");
        require(b.release_producer(i),"slot release");
    }
    b.stop(); auto s=b.final_snapshot(true); actual_thread=11;
    require(s.histograms_available&&s.registered_producers==32&&raster(s).observations==32&&raster(s).total_units==32,"all fixed slots counted exactly");
}
void final_requires_external_and_private_quiescence() {
    fake_clock c; bank b(c); start(b);
    unavailable(b.final_snapshot(true),phase::status::busy);
    scope s(b,0,phase::metric::raster_qpc); b.stop();
    const u64 calls=c.calls.load(); { scope rejected(b,0,phase::metric::raster_qpc); require(!rejected,"no new scope after stop"); }
    unsigned mono=0; b.observe_lateness(0,phase::metric::task_due_late_us,1.0,[&]{++mono;return 2.0;});
    require(c.calls==calls&&mono==0,"stop rejects new clock reads");
    require(b.release_producer(0),"detach may precede admitted scope completion");
    unavailable(b.final_snapshot(true),phase::status::busy);
    c.value=120; require(s.finish(),"admitted scope finishes after stop and detach");
    unavailable(b.final_snapshot(false),phase::status::busy);
    auto result=b.final_snapshot(true); require(result.histograms_available&&raster(result).total_units==20,"only final complete exposes histogram");
}
void wake_domains_rounding_and_skips() {
    fake_clock c; bank b(c); start(b); unsigned calls=0;
    require(b.observe_lateness(0,phase::metric::shared_wake_late_us,1.0,[&]{++calls;return 1.25;}),"shared lateness");
    require(!b.observe_lateness(0,phase::metric::shared_wake_late_us,2.0,[&]{++calls;return 1.5;}),"early separate");
    require(!b.observe_lateness(0,phase::metric::shared_wake_late_us,0.0,[&]{++calls;return 1.5;}),"missing due separate");
    require(b.observe_lateness(0,phase::metric::shared_wake_late_us,1.5,[&]{++calls;return 1.5;}),"zero lateness valid");
    require(b.observe_lateness(0,phase::metric::task_due_late_us,2.0,[&]{++calls;return 2.5;}),"task population separate");
    require(b.observe_lateness(0,phase::metric::task_due_late_us,3.0,[&]{++calls;return 3.0+1.0/1048576;}),"positive sub-microsecond rounds upward");
    auto s=finish(b); require(c.calls==0&&calls==5,"WTF lateness never reads QPC");
    require(s.metrics[0].observations==2&&s.metrics[0].total_units==250000&&s.metrics[1].total_units==500001,"separate microsecond populations");
    require(s.early_wakes==1&&s.missing_due==1,"early/missing are not fabricated zero samples");
}
void qpc_faults_have_no_partial_histograms() {
    for (unsigned kind=0;kind<3;++kind) {
        fake_clock c; bank b(c); start(b);
        if (kind==2) {
            { scope s(b,0,phase::metric::raster_qpc); c.value=110; require(s.finish(),"first sample"); }
            c.value=109; scope reversed(b,0,phase::metric::raster_qpc); require(!reversed,"backward next begin");
        } else {
            if (!kind) c.value=0;
            scope s(b,0,phase::metric::raster_qpc);
            if (kind) { c.value=99; require(!s.finish(),"backward end"); }
        }
        auto s=finish(b); unavailable(s,phase::status::fault); require(s.faults&phase::clock_fault,"clock fault visible");
    }
}
void bad_due_and_monotonic_clock_faults() {
    for (unsigned kind=0;kind<4;++kind) {
        fake_clock c; bank b(c); start(b); unsigned reads=0;
        if (kind==3) require(b.observe_lateness(0,phase::metric::task_due_late_us,1.0,[]{return 2.0;}),"first monotonic sample");
        const double due=kind==0?std::numeric_limits<double>::infinity():1.0;
        b.observe_lateness(0,phase::metric::task_due_late_us,due,[&]{++reads;return kind==1?std::numeric_limits<double>::quiet_NaN():kind==3?1.0:1.0e20;});
        auto s=finish(b); unavailable(s,phase::status::fault);
        require(s.faults&(kind==0?phase::invalid_due:kind==2?phase::saturated:phase::clock_fault),"explicit due/clock/conversion fault");
        require(c.calls==0&&(kind!=0||reads==0),"no QPC or fallback clock");
    }
}
void binding_and_unit_faults_precede_timing() {
    for (unsigned kind=0;kind<5;++kind) {
        fake_clock c; bank b(c); start(b); unsigned reads=0;
        if (kind==0) actual_thread=12;
        if (kind<2) { scope s(b,kind==1?32:0,phase::metric::raster_qpc); require(!s,"wrong owner or slot"); }
        else if (kind==2) { scope s(b,0,phase::metric::shared_wake_late_us); require(!s,"wrong unit for QPC scope"); }
        else if (kind==3) b.observe_lateness(0,phase::metric::raster_qpc,1.0,[&]{++reads;return 2.0;});
        else { scope s(b,0,phase::metric::raster_qpc); actual_thread=12; require(!s.finish(),"wrong owner cannot finish"); }
        actual_thread=11; auto s=finish(b); unavailable(s,phase::status::fault);
        require((s.faults&phase::binding_fault)&&c.calls==(kind==4?1U:0U)&&reads==0,"binding/unit failure must precede any new timing");
    }
}
void saturation_invalidates_inclusive_totals() {
    fake_clock c; bank b(c); start(b);
    scope outer(b,0,phase::metric::raster_qpc); c.value=101;
    scope inner(b,0,phase::metric::raster_qpc); c.value=(std::numeric_limits<u64>::max)()-1;
    require(inner.finish(),"first huge inclusive sample fits"); c.value=(std::numeric_limits<u64>::max)();
    require(!outer.finish(),"overlapping inclusive totals cannot wrap");
    auto s=finish(b); unavailable(s,phase::status::fault); require(s.faults&phase::saturated,"saturation exposed");
}
void outcomes_and_finish_once() {
    fake_clock c; bank b(c); start(b);
    for (unsigned i=0;i<3;++i) { scope s(b,0,phase::metric::raster_qpc); c.value.fetch_add(1); require(s.finish(static_cast<phase::outcome>(i)),"outcome observation"); require(!s.finish(),"finish once"); }
    { scope s(b,0,phase::metric::raster_qpc); c.value.fetch_add(1); }
    auto s=finish(b); require(raster(s).observations==4,"destructor does not duplicate an explicit finish");
    for (u64 n:raster(s).outcomes) require(n==1,"return/cancel/api failure/abandoned stay distinct");
}
void reconfiguration_never_resets_evidence() {
    fake_clock c; bank b(c); start(b);
    { scope s(b,0,phase::metric::raster_qpc); c.value=105; s.finish(); }
    require(!b.configure(false,session()),"late disable rejected"); auto s=finish(b);
    require(!b.configure(true,session()),"restart rejected");
    require(s.histograms_available&&raster(s).total_units==5&&raster(b.final_snapshot(true)).total_units==5,"old evidence remains");
}
void actual_thread_finishes_after_controller_stop() {
    fake_clock c; bank b(c); require(b.configure(true,session()),"configure");
    std::atomic<bool> ready{false},proceed{false},ok{false};
    std::thread worker([&]{
        actual_thread=22;
        const bool registered=b.register_producer(0,22);
        scope span(b,0,phase::metric::raster_qpc);
        const bool released=b.release_producer(0);
        ready.store(true,std::memory_order_release);
        while (!proceed.load(std::memory_order_acquire)) std::this_thread::yield();
        ok.store(registered&&released&&span.finish(),std::memory_order_release);
    });
    const auto due=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (!ready.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<due) std::this_thread::yield();
    const bool admitted=ready.load(std::memory_order_acquire); b.stop();
    const auto busy=b.final_snapshot(true); c.value=150; proceed.store(true,std::memory_order_release); worker.join();
    require(admitted&&ok.load(),"bounded worker completion"); unavailable(busy,phase::status::busy);
    auto s=b.final_snapshot(true); require(s.histograms_available&&raster(s).total_units==50,"stop did not reclaim an inflight slot");
}

int main() {
    const struct { const char* name; void (*run)(); } tests[]={
        {"off and unregistered",off_and_unregistered_do_not_read_clocks},
        {"nested inclusive",nested_scopes_are_independent_inclusive_wall},
        {"histogram boundaries",logarithmic_boundaries_and_highest_bucket},
        {"resume and tid reuse",resume_and_recycled_thread_id_keep_slot_history},
        {"32 fixed trace slots",all_thirty_two_trace_slots_aggregate},
        {"stop and quiescence",final_requires_external_and_private_quiescence},
        {"separate wake domains",wake_domains_rounding_and_skips},
        {"QPC faults",qpc_faults_have_no_partial_histograms},
        {"due/clock/conversion faults",bad_due_and_monotonic_clock_faults},
        {"binding and units",binding_and_unit_faults_precede_timing},
        {"saturation",saturation_invalidates_inclusive_totals},
        {"outcomes and once",outcomes_and_finish_once},
        {"cold configuration",reconfiguration_never_resets_evidence},
        {"thread stop/inflight",actual_thread_finishes_after_controller_stop}
    };
    try { for (const auto& t:tests) { t.run(); std::cout<<"PASS "<<t.name<<'\n'; } }
    catch (const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
    std::cout<<"14 contracts passed; CPU-only, no native GUI or performance qualification\n";
    return 0;
}

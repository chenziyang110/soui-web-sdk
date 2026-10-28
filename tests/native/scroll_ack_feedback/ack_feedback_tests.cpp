#include "cc/trees/ScrollTreeSnapshot.h"
#ifdef SOUI_ACK_BASELINE
#include "baseline_ack_shim.h"
#else
#include "cc/input/CompositorScrollAcknowledgement.h"
#include "third_party/WebKit/public/platform/WebLayerScrollClient.h"
#endif
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace cc;
#ifdef SOUI_ACK_BASELINE
using Ack = WebLayerImpl;
#else
using Ack = CompositorScrollAcknowledgement;
#endif
constexpr double epsilon = 0.00001;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool near(double a, double b) { return std::isfinite(a) && std::isfinite(b) && std::fabs(a-b) < epsilon; }
void quote(const char* s) {
    std::cout << '"';
    for (; *s; ++s) { if (*s == '"' || *s == '\\') std::cout << '\\'; std::cout << *s; }
    std::cout << '"';
}
void vectorEqual(ScrollVector value, ScrollVector expected, const char* message) {
    require(near(value.x,expected.x) && near(value.y,expected.y),message);
}
void checkAck(ScrollVector total, ScrollVector visual, ScrollVector canonical, ScrollVector expected) {
    Ack ack;
    const uint64_t epoch=ack.epoch();
    require(ack.accept(epoch,1,total,visual,canonical),"valid ACK rejected");
    std::cout << "{\"event\":\"ack\",\"epoch\":" << ack.epoch() << ",\"sample\":" << ack.sample()
        << ",\"total_x\":" << ack.total().x << ",\"total_y\":" << ack.total().y << "}\n";
    require(ack.epoch()==epoch,"canonical feedback changed compositor epoch");
    require(ack.sample()==1,"canonical feedback discarded accepted sample");
    vectorEqual(ack.total(),expected,"canonical ACK is in the wrong cumulative coordinates");
    vectorEqual(ScrollVector(canonical.x-ack.total().x,canonical.y-ack.total().y),
        ScrollVector(visual.x-total.x,visual.y-total.y),"canonical ACK changed coordinate origin");
}
void positiveFractional() { checkAck({0,42.75},{0,42.75},{0,42},{0,42}); }
void negativeDualNonzeroOrigin() { checkAck({5.75,-8.25},{12.75,-3.25},{12,-4},{5,-9}); }
void integerIdentity() { checkAck({4,18},{11,25},{11,25},{4,18}); }
void canonicalClamp() { checkAck({0,20.5},{0,120.5},{0,100},{0,0}); }
void sameCanonicalMultipleSamples() {
    Ack ack; const uint64_t epoch=ack.epoch();
    require(ack.accept(epoch,1,{0,10},{0,10},{0,10}),"initial integer ACK rejected");
    require(ack.accept(epoch,2,{0,10.25},{0,10.25},{0,10}),"same-canonical new sample rejected");
    require(ack.epoch()==epoch && ack.sample()==2,"same canonical sample cancelled or lost receipt");
    vectorEqual(ack.total(),{0,10},"same canonical sample published raw total");
    require(ack.accept(epoch,3,{0,10.75},{0,10.75},{0,10}),"second same-canonical sample rejected");
    require(ack.epoch()==epoch && ack.sample()==3,"new sample did not advance when canonical was equal");
    vectorEqual(ack.total(),{0,10},"same canonical ACK drifted");
}
void fences() {
    Ack ack; const uint64_t epoch=ack.epoch();
    require(ack.accept(epoch,7,{0,12},{0,12},{0,12}),"valid fence fixture rejected");
    require(!ack.accept(epoch,7,{0,13},{0,13},{0,13}),"duplicate sample was accepted as a fresh ACK");
    require(!ack.accept(epoch,6,{0,13},{0,13},{0,13}),"old sample accepted");
    require(!ack.accept(epoch+1,8,{0,13},{0,13},{0,13}),"wrong epoch accepted");
    require(ack.epoch()==epoch && ack.sample()==7,"fence rejection changed identity");
    vectorEqual(ack.total(),{0,12},"fence rejection changed total");
}
void rejectsWithoutMutation(ScrollVector total, ScrollVector visual, ScrollVector canonical) {
    Ack ack; const uint64_t epoch=ack.epoch();
    require(ack.accept(epoch,1,{1,2},{1,2},{1,2}),"finite fixture rejected");
    require(!ack.accept(epoch,2,total,visual,canonical),"invalid ACK accepted");
    require(ack.epoch()==epoch && ack.sample()==1,"invalid ACK partially changed identity");
    vectorEqual(ack.total(),{1,2},"invalid ACK partially changed total");
#ifndef SOUI_ACK_BASELINE
    vectorEqual(ack.canonicalAdjustment(),{},"invalid ACK partially changed adjustment");
    require(ack.matchesRaw(epoch,1,{1,2},{1,2}),"invalid ACK changed the saved raw receipt");
#endif
}
void nonfiniteRaw() {
    rejectsWithoutMutation({std::numeric_limits<double>::quiet_NaN(),0},{0,0},{0,0});
    rejectsWithoutMutation({0,0},{0,std::numeric_limits<double>::infinity()},{0,0});
    rejectsWithoutMutation({0,0},{-std::numeric_limits<double>::infinity(),0},{0,0});
}
void nonfiniteCanonical() {
    rejectsWithoutMutation({0,1},{0,1},{0,std::numeric_limits<double>::quiet_NaN()});
    rejectsWithoutMutation({0,1},{0,1},{std::numeric_limits<double>::infinity(),0});
}
void finiteOverflow() {
    const double big=std::numeric_limits<double>::max();
    rejectsWithoutMutation({0,0},{-big,0},{big,0});
    rejectsWithoutMutation({big,0},{-big/2,0},{big/2,0});
}
void finiteOriginOverflow() {
    const double big=std::numeric_limits<double>::max();
    rejectsWithoutMutation({-big,0},{big,0},{big,0});
}

#ifndef SOUI_ACK_BASELINE
void defaultNormalizerIdentity() {
    struct Client : blink::WebLayerScrollClient { int calls=0; void didScroll() override { ++calls; } } client;
    blink::WebDoublePoint visual(12.75,-3.25),canonical(999,999);
    require(client.canonicalizeCompositorScrollPosition(visual,canonical),"default normalizer rejected identity");
    require(canonical.x==visual.x && canonical.y==visual.y,"default normalizer changed identity");
    require(client.calls==0,"default normalization emitted a callback");
}
void duplicateRawReceipt() {
    Ack ack;
    require(ack.accept(ack.epoch(),7,{5.75,-8.25},{12.75,-3.25},{12,-4}),"raw receipt fixture rejected");
    require(ack.matchesRaw(ack.epoch(),7,{5.75,-8.25},{12.75,-3.25}),"original raw receipt lost after canonicalization");
    require(!ack.matchesRaw(ack.epoch(),7,ack.total(),{12,-4}),"canonical fields were mistaken for raw receipt");
    require(!ack.matchesRaw(ack.epoch()+1,7,{5.75,-8.25},{12.75,-3.25}),"wrong receipt epoch accepted");
    require(!ack.matchesRaw(ack.epoch(),6,{5.75,-8.25},{12.75,-3.25}),"old receipt sample accepted");
    require(!ack.matchesRaw(ack.epoch(),7,{5.75,-8.23},{12.75,-3.25}),"conflicting raw receipt accepted");
    require(ack.matchesRaw(ack.epoch(),7,{5.754,-8.25},{12.75,-3.254}),"existing raw receipt tolerance lost");
    require(!ack.matchesRaw(ack.epoch(),7,{5.75,-8.25},{12.75,std::numeric_limits<double>::infinity()}),"nonfinite raw receipt accepted");
}
void explicitInvalidation() {
    Ack ack; const uint64_t epoch=ack.epoch();
    require(ack.accept(epoch,7,{0,12.75},{0,12.75},{0,12}),"invalidation fixture rejected");
    ack.invalidate();
    require(ack.epoch()!=epoch && ack.sample()==0,"explicit invalidation retained old identity");
    vectorEqual(ack.total(),{},"explicit invalidation retained ACK");
    vectorEqual(ack.canonicalAdjustment(),{},"explicit invalidation retained adjustment");
    require(!ack.matchesRaw(epoch,7,{0,12.75},{0,12.75}),"explicit invalidation retained raw receipt");
    require(!ack.accept(epoch,8,{0,13},{0,13},{0,13}),"late ACK resurrected invalidated epoch");
}
void initialZeroSampleReceipt() {
    Ack ack; const uint64_t epoch=ack.epoch();
    require(ack.matchesInitial(epoch,0,{0,0},{12,-3},{12,-3}),"initial zero-sample barrier rejected");
    require(ack.matchesInitial(epoch,0,{0.004,-0.004},{12.004,-3.004},{12,-3}),"initial receipt tolerance changed");
    require(!ack.matchesRaw(epoch,0,{0,0},{12,-3}),"initial check manufactured a raw receipt");
    require(!ack.matchesInitial(epoch+1,0,{0,0},{12,-3},{12,-3}),"wrong initial epoch accepted");
    require(!ack.matchesInitial(epoch,1,{0,0},{12,-3},{12,-3}),"nonzero initial sample accepted");
    require(!ack.matchesInitial(epoch,0,{0,0.02},{12,-3},{12,-3}),"nonzero initial total accepted");
    require(!ack.matchesInitial(epoch,0,{0,0},{12,-3.02},{12,-3}),"initial position conflict accepted");
    require(!ack.matchesInitial(epoch,0,{0,0},{12,-3},{12,std::numeric_limits<double>::infinity()}),"nonfinite initial base accepted");
    require(ack.sample()==0 && ack.epoch()==epoch,"initial receipt query mutated identity");
    vectorEqual(ack.total(),{},"initial receipt query mutated total");
    require(ack.accept(epoch,1,{0,12.75},{0,12.75},{0,12}),"initial receipt fixture sample rejected");
    require(!ack.matchesInitial(epoch,0,{0,0},{0,12},{0,12}),"initial proof accepted after a real sample");
    ack.invalidate();
    require(ack.matchesInitial(ack.epoch(),0,{0,0},{12,-3},{12,-3}),"new epoch initial proof rejected after invalidate");
    require(!ack.matchesRaw(ack.epoch(),0,{0,0},{12,-3}),"invalidation manufactured raw receipt");
}

struct Fixture {
    Ack ack;
    ScrollController controller;
    ScrollNodeSnapshot input;
    uint64_t sequence=0;
    double time=1;
    Fixture(double base=0,double minimum=0,double maximum=1000,bool limited=false) {
        input.key=ScrollNodeKey(1,2,3,4);
        input.sceneVersion=1; input.scrollEpoch=ack.epoch();
        input.baseOffset={0,base}; input.minimumOffset={0,minimum}; input.maximumOffset={0,maximum};
        input.viewportClip=ScrollRect(0,0,300,300);
        input.inputAxes=ScrollInputAxisVertical; input.mainThreadReasons=ScrollMainThreadReasonNone;
        input.coverageComplete=true; input.coverageMinimum=input.minimumOffset;
        input.coverageMaximum={0,limited ? 300 : maximum}; input.setInputPrediction(input.baseOffset);
        require(controller.activateScene(input),"initial controller scene rejected");
    }
    void safe() const {
        const ScrollVector visual=controller.visualOffset(); const auto& n=controller.activeNode();
        require(std::isfinite(visual.y) && visual.y>=n.coverageMinimum.y-epsilon && visual.y<=n.coverageMaximum.y+epsilon,
            "visual escaped prepared coverage");
    }
    void push(double delta,bool smooth=true) {
        require(input.advanceInputPrediction({0,delta}),"fixture input admission rejected");
        require(controller.enqueueDelta(++sequence,{0,delta},time,smooth),"fixture controller input rejected");
    }
    void frame() { time+=0.016; controller.sample(time); safe(); }
    void settle() { for(int i=0;i<240;++i) frame(); }
    void trace(const char* step) const {
        const auto b=controller.captureBarrier();
        std::cout << "{\"event\":\"controller\",\"step\":";quote(step);
        std::cout << ",\"epoch\":" << b.scrollEpoch << ",\"sample\":" << b.sampleSequence
            << ",\"visual_y\":" << b.visualOffset.y << ",\"raw_total_y\":" << b.consumedTotal.y
            << ",\"ack_total_y\":" << ack.total().y << ",\"canonical_adjustment_y\":" << ack.canonicalAdjustment().y
            << ",\"active\":" << (controller.needsAnimationFrame()?"true":"false") << "}\n";
    }
    void publish(bool expand=false) {
        const auto receipt=controller.captureBarrier();
        const ScrollVector canonical(std::floor(receipt.visualOffset.x),std::floor(receipt.visualOffset.y));
        require(ack.accept(receipt.scrollEpoch,receipt.sampleSequence,receipt.consumedTotal,receipt.visualOffset,canonical),
            "controller raw sample rejected by production ACK helper");
        const ScrollVector prediction=input.inputPrediction;
        ScrollNodeSnapshot next=controller.activeNode(); ++next.sceneVersion;
        next.baseOffset=canonical; next.acknowledgedSample=ack.sample(); next.acknowledgedTotal=ack.total();
        next.canonicalAdjustment=ack.canonicalAdjustment();
        if(expand){next.coverageMinimum=next.minimumOffset;next.coverageMaximum=next.maximumOffset;}
        next.setInputPrediction(canonical);
        require(controller.activateScene(next),"canonical same-epoch scene rejected");
        input=next; input.setInputPrediction(prediction); safe();
        require(ack.matchesRaw(receipt.scrollEpoch,receipt.sampleSequence,receipt.consumedTotal,receipt.visualOffset),
            "controller raw barrier no longer matches canonical receipt");
    }
    void resetExternal(double value) {
        ack.invalidate();
        ScrollNodeSnapshot next=controller.activeNode(); ++next.sceneVersion;
        next.scrollEpoch=ack.epoch(); next.baseOffset={0,value};next.acknowledgedSample=0;
        next.acknowledgedTotal={};next.canonicalAdjustment={};next.setInputPrediction(next.baseOffset);
        require(controller.activateScene(next),"external invalidation scene rejected"); input=next; safe();
    }
    void endpoint(double target) {
        require(near(controller.visualOffset().y,target),"controller target endpoint differs");
        require(!controller.needsAnimationFrame(),"controller did not settle");
    }
};
void fractionalInFlight() {
    Fixture f;f.push(350);f.frame();
    const double before=f.controller.visualOffset().y;
    require(before>0 && before<350 && !near(before,std::floor(before)),"fixture did not produce a fractional sample");
    f.publish();require(near(f.controller.visualOffset().y,before),"canonical activation jumped visual position");
    require(f.controller.needsAnimationFrame(),"canonical ACK cancelled retained target");f.settle();f.endpoint(350);f.trace("fractional-endpoint");
}
void continuousAcks() {
    Fixture f; f.push(500);
    for(int i=0;i<32;++i){f.frame();f.publish();}
    f.settle();f.endpoint(500);f.trace("continuous-acks-endpoint");
}
void reverseAfterCanonical() {
    Fixture f;f.push(350);f.frame();f.publish();f.push(-150);f.settle();f.endpoint(200);
}
void negativeNonzeroBase() {
    Fixture f(700);f.push(-350);f.frame();f.publish();f.settle();f.endpoint(350);
    require(f.controller.captureBarrier().consumedTotal.y<0,"nonzero base fixture did not produce negative cumulative motion");
}
void coverageExpansion() {
    Fixture f(0,0,1000,true);f.push(350);f.frame();f.publish();f.settle();f.endpoint(300);
    f.publish(true);f.settle();f.endpoint(350);f.trace("coverage-expanded-endpoint");
}
void maximumNearEpsilon() {
    Fixture f(0,0,400);f.push(600);f.settle();f.endpoint(400);
    const double value=f.controller.visualOffset().y;
    require(value<400 && value>399.99999,"real integration did not exercise max-minus-epsilon floor");
    f.trace("max-before-floor");f.publish();f.trace("max-after-floor");
    require(!f.input.canConsumeDelta({0,100}),"canonical floor falsely restored outward capacity at real max");
    require(!f.input.advanceInputPrediction({0,100}),"outward input accepted at settled max");
}
void minimumNearEpsilon() {
    Fixture f(0,-400,400);f.push(-600);f.settle();f.endpoint(-400);f.publish();
    require(!f.input.canConsumeDelta({0,-100}),"canonical floor restored outward capacity at real min");
}
void equalNormalizedExternalReset() {
    Fixture f;f.push(350);f.frame();f.publish();
    const double canonical=f.input.baseOffset.y;
    require(f.controller.needsAnimationFrame(),"reset fixture already settled");
    f.resetExternal(canonical);f.settle();f.endpoint(canonical);f.trace("external-equal-normalized-cancelled");
}
void changedExternalReset() {
    Fixture f;f.push(350);f.frame();f.publish();f.resetExternal(0);f.settle();f.endpoint(0);
}
void invalidAdjustmentSnapshot() {
    Fixture f;vectorEqual(f.input.canonicalAdjustment,{},"default snapshot adjustment is nonzero");
    auto bad=f.input;++bad.sceneVersion;bad.canonicalAdjustment.y=std::numeric_limits<double>::infinity();
    require(!bad.isValid(),"snapshot validation accepted nonfinite adjustment");
    require(!f.controller.activateScene(bad),"nonfinite canonical adjustment scene accepted");
    vectorEqual(f.controller.visualOffset(),{},"invalid adjustment mutated active visual");
}
void finiteEffectiveBaseOverflow() {
    Fixture f;auto bad=f.input;++bad.sceneVersion;const double big=std::numeric_limits<double>::max();
    bad.baseOffset.y=big;bad.maximumOffset.y=big;bad.coverageMaximum.y=big;
    bad.canonicalAdjustment.y=-big;bad.setInputPrediction(bad.baseOffset);
    require(!bad.isValid(),"snapshot validation accepted finite effective-base overflow");
    require(!f.controller.activateScene(bad),"finite effective-base subtraction overflow accepted");
    vectorEqual(f.controller.visualOffset(),{},"effective-base overflow mutated visual");
    require(f.controller.activeNode().sceneVersion==1,"effective-base overflow mutated scene");
}
#endif

using Test=void(*)();
}
int main() {
    std::cout << std::setprecision(17);
    const std::vector<std::pair<const char*,Test>> tests={
        {"positive-fractional-feedback",positiveFractional},
        {"negative-dual-nonzero-origin",negativeDualNonzeroOrigin},
        {"integer-identity",integerIdentity},{"canonical-clamp-origin",canonicalClamp},
        {"same-canonical-new-samples",sameCanonicalMultipleSamples},{"epoch-and-sample-fences",fences},
        {"nonfinite-raw-rejected",nonfiniteRaw},{"nonfinite-canonical-rejected",nonfiniteCanonical},
        {"finite-arithmetic-overflow-rejected",finiteOverflow},{"finite-origin-overflow-rejected",finiteOriginOverflow},
#ifndef SOUI_ACK_BASELINE
        {"default-normalizer-identity",defaultNormalizerIdentity},
        {"duplicate-raw-receipt",duplicateRawReceipt},{"explicit-invalidation",explicitInvalidation},
        {"initial-zero-sample-receipt",initialZeroSampleReceipt},
        {"controller-fractional-inflight",fractionalInFlight},{"controller-continuous-acks",continuousAcks},
        {"controller-reverse-after-ack",reverseAfterCanonical},{"controller-negative-nonzero-base",negativeNonzeroBase},
        {"controller-coverage-expansion",coverageExpansion},{"controller-real-max-floor",maximumNearEpsilon},
        {"controller-real-min-floor",minimumNearEpsilon},{"external-equal-normalized-state-reset",equalNormalizedExternalReset},
        {"external-changed-state-reset",changedExternalReset},{"snapshot-invalid-adjustment",invalidAdjustmentSnapshot},
        {"snapshot-effective-base-overflow",finiteEffectiveBaseOverflow},
#endif
    };
#ifdef SOUI_ACK_BASELINE
    std::cout << "{\"event\":\"metadata\",\"variant\":\"extracted-old-production-methods\",\"full_dom_path_tested\":false}\n";
#else
    std::cout << "{\"event\":\"metadata\",\"variant\":\"production-helper-and-controller\",\"full_dom_path_tested\":false}\n";
#endif
    int passed=0;
    for(const auto& test:tests){
        try{test.second();++passed;std::cout << "{\"event\":\"test\",\"name\":";quote(test.first);std::cout << ",\"passed\":true}\n";}
        catch(const std::exception& e){std::cout << "{\"event\":\"test\",\"name\":";quote(test.first);std::cout << ",\"passed\":false,\"message\":";quote(e.what());std::cout << "}\n";}
    }
    std::cout << "{\"event\":\"result\",\"passed\":" << passed << ",\"total\":" << tests.size() << "}\n";
    return passed==static_cast<int>(tests.size())?0:1;
}

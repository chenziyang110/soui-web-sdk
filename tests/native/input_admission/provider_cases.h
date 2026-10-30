#pragma once

#include "../../../sdk/src/host/provider_owner_deadline.h"
#include "deadline_cases.h"
#include <future>

namespace provider_deadline_cases {
using namespace soui::sdk;
using input_deadline_cases::check;
using clock = std::chrono::steady_clock;

struct provider_probe final {
  unsigned legacy{0}, bounded{0};
  int status{WKE_INPUT_UI_OK};
  unsigned remaining{0};
  unsigned long long deadline{0}, frequency{0};
};
inline provider_probe probe;
inline void WKE_INPUT_UI_CALL legacy() { ++probe.legacy; }
inline int WKE_INPUT_UI_CALL bounded(unsigned long long deadline, unsigned long long frequency, unsigned int* more) {
  ++probe.bounded; probe.deadline = deadline; probe.frequency = frequency;
  *more = probe.remaining; return probe.status;
}

inline void run_bridge() {
  const auto after = clock::time_point(std::chrono::duration_cast<clock::duration>(std::chrono::seconds(-1234)));
  const auto deadline = after + std::chrono::nanoseconds(1234);
  std::uint64_t output = 99;
  check(detail::map_provider_owner_deadline(deadline, after, 7000000, 10000000, output) && output == 7000012,
      "provider bridge mixed epochs or rounded up the remaining duration");
  check(detail::map_provider_owner_deadline(after, after, 7000000, 10000000, output) && output == 7000000,
      "provider bridge did not retain an expired cutoff");
  check(detail::map_provider_owner_deadline(after - std::chrono::seconds(1), after, 0, 10000000, output) && output == 0,
      "provider bridge treated a zero expired cutoff as unbounded");
  check(!detail::map_provider_owner_deadline(deadline, after, 7000000, 0, output), "provider bridge accepted zero frequency");
  constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
  check(!detail::map_provider_owner_deadline(after + std::chrono::seconds(1), after, maximum, 10000000, output),
      "provider bridge did not reject QPC addition overflow");
  check(!detail::map_provider_owner_deadline(after + std::chrono::seconds(1), after, 0, maximum, output),
      "provider bridge did not reject multiplication overflow");

  detail::provider_owner_callbacks callbacks{legacy, bounded};
  unsigned samples = 0;
  auto sample = [&](std::uint64_t& qpc, std::uint64_t& frequency, clock::time_point& steady) {
    ++samples; qpc = 7000000; frequency = 10000000; steady = after; return true;
  };
  probe = {}; probe.remaining = 1;
  auto result = detail::service_provider_owner_until(callbacks, deadline, sample);
  check(result.work_remaining && result.status == detail::provider_owner_service_status::bounded
      && probe.legacy == 0 && probe.bounded == 1 && probe.deadline == 7000012 && samples == 1,
      "provider bounded bridge double-dispatched or changed the cutoff");
  probe = {}; probe.status = WKE_INPUT_UI_UNAVAILABLE; probe.remaining = 1;
  result = detail::service_provider_owner_until(callbacks, deadline, sample);
  check(!result.work_remaining && result.status == detail::provider_owner_service_status::provider_fallback
      && probe.legacy == 1 && probe.bounded == 1, "unavailable provider did not fall back exactly once");
  for (const auto status : {WKE_INPUT_UI_BUSY, WKE_INPUT_UI_INVALID, WKE_INPUT_UI_REJECTED}) {
    probe = {}; probe.status = status; probe.remaining = 1;
    result = detail::service_provider_owner_until(callbacks, deadline, sample);
    check(!result.work_remaining && result.status == detail::provider_owner_service_status::rejected
        && probe.legacy == 0 && probe.bounded == 1, "provider status became continuation or repeated legacy work");
  }
  probe = {};
  result = detail::service_provider_owner_until(callbacks, deadline,
      [](std::uint64_t&, std::uint64_t&, clock::time_point&) { return false; });
  check(!result.work_remaining && result.status == detail::provider_owner_service_status::bridge_fallback
      && probe.legacy == 1 && probe.bounded == 0, "failed QPC sample did not use pre-work fallback");
  probe = {};
  result = detail::service_provider_owner_until(callbacks, deadline,
      [&](std::uint64_t& qpc, std::uint64_t& frequency, clock::time_point& steady) {
        qpc = maximum; frequency = 10000000; steady = after; return true;
      });
  check(result.status == detail::provider_owner_service_status::bridge_fallback && probe.legacy == 1 && probe.bounded == 0,
      "bridge overflow reached provider work");
}

inline void run_abi() {
  struct legacy_prefix {
    unsigned int structSize, abiVersion;
    unsigned long long capabilities;
    decltype(wkeInputUiApi::traceConfigure) traceConfigure;
    decltype(wkeInputUiApi::traceStop) traceStop;
    decltype(wkeInputUiApi::traceSnapshot) traceSnapshot;
    decltype(wkeInputUiApi::traceDrain) traceDrain;
    decltype(wkeInputUiApi::serviceOwnerOpportunity) serviceOwnerOpportunity;
    decltype(wkeInputUiApi::setUiMirrorConsumerRequired) setUiMirrorConsumerRequired;
    decltype(wkeInputUiApi::uiMirrorConsumerRequired) uiMirrorConsumerRequired;
    decltype(wkeInputUiApi::setInputTraceContext) setInputTraceContext;
    decltype(wkeInputUiApi::cancelInputState) cancelInputState;
    decltype(wkeInputUiApi::setViewTraceIdentity) setViewTraceIdentity;
    decltype(wkeInputUiApi::setHostVisible) setHostVisible;
    decltype(wkeInputUiApi::retainTraceProducer) retainTraceProducer;
    decltype(wkeInputUiApi::releaseTraceProducer) releaseTraceProducer;
  };
#define CHECK_PROVIDER_OFFSET(field) static_assert(offsetof(legacy_prefix, field) == offsetof(wkeInputUiApi, field), "provider v1 prefix moved")
  CHECK_PROVIDER_OFFSET(structSize); CHECK_PROVIDER_OFFSET(abiVersion); CHECK_PROVIDER_OFFSET(capabilities);
  CHECK_PROVIDER_OFFSET(traceConfigure); CHECK_PROVIDER_OFFSET(traceStop); CHECK_PROVIDER_OFFSET(traceSnapshot);
  CHECK_PROVIDER_OFFSET(traceDrain); CHECK_PROVIDER_OFFSET(serviceOwnerOpportunity);
  CHECK_PROVIDER_OFFSET(setUiMirrorConsumerRequired); CHECK_PROVIDER_OFFSET(uiMirrorConsumerRequired);
  CHECK_PROVIDER_OFFSET(setInputTraceContext); CHECK_PROVIDER_OFFSET(cancelInputState);
  CHECK_PROVIDER_OFFSET(setViewTraceIdentity); CHECK_PROVIDER_OFFSET(setHostVisible);
  CHECK_PROVIDER_OFFSET(retainTraceProducer); CHECK_PROVIDER_OFFSET(releaseTraceProducer);
#undef CHECK_PROVIDER_OFFSET
  static_assert(offsetof(wkeInputUiApi, serviceOwnerOpportunityUntil)
      == offsetof(wkeInputUiApi, releaseTraceProducer) + sizeof(wkeInputUiApi::releaseTraceProducer));
  wkeInputUiApi api{};
  api.structSize = sizeof(api); api.abiVersion = WKE_INPUT_UI_ABI_VERSION;
  api.capabilities = WKE_INPUT_UI_CAP_OWNER_OPPORTUNITY | WKE_INPUT_UI_CAP_OWNER_DEADLINE;
  api.serviceOwnerOpportunity = legacy; api.serviceOwnerOpportunityUntil = bounded;
  auto selected = detail::select_provider_owner_callbacks(&api);
  check(selected.legacy == legacy && selected.bounded == bounded, "new provider tail was not selected");
  api.structSize = offsetof(wkeInputUiApi, serviceOwnerOpportunityUntil);
  selected = detail::select_provider_owner_callbacks(&api);
  check(selected.legacy == legacy && !selected.bounded, "short provider table exposed tail pointer");
  api.structSize = sizeof(api); api.capabilities = WKE_INPUT_UI_CAP_OWNER_OPPORTUNITY;
  selected = detail::select_provider_owner_callbacks(&api);
  check(selected.legacy == legacy && !selected.bounded, "old provider capability exposed tail pointer");
  api.capabilities |= WKE_INPUT_UI_CAP_OWNER_DEADLINE; api.serviceOwnerOpportunityUntil = nullptr;
  selected = detail::select_provider_owner_callbacks(&api);
  check(selected.legacy == legacy && !selected.bounded, "null provider tail did not retain legacy");
  api.abiVersion += 1;
  check(!detail::select_provider_owner_callbacks(&api).legacy, "unknown provider ABI accepted");
  api.abiVersion = WKE_INPUT_UI_ABI_VERSION; api.structSize = 4;
  check(!detail::select_provider_owner_callbacks(&api).legacy && !detail::select_provider_owner_callbacks(nullptr).legacy,
      "invalid provider header accepted");
}

#ifdef _WIN32
inline void run_configuration(const wchar_t* value, bool finite, bool new_provider) {
  input_deadline_cases::scoped_budget configuration(value);
  engine_thread engine;
  std::atomic<unsigned> legacy_calls{0}, bounded_calls{0};
  std::promise<clock::time_point> called;
  auto once = std::make_shared<std::atomic<bool>>(false);
  const auto report = [&](clock::time_point deadline) { if (!once->exchange(true)) called.set_value(deadline); };
  check(detail::engine_admission::install_owner_opportunity(engine,
      [&] { ++legacy_calls; report((clock::time_point::max)()); }, new_provider
          ? std::function<bool(clock::time_point)>([&](clock::time_point deadline) { ++bounded_calls; report(deadline); return false; })
          : std::function<bool(clock::time_point)>{}), "provider callback cold installation");
  check(engine.start([] { return true; }), "provider configuration engine startup");
  check(!detail::engine_admission::install_owner_opportunity(engine, {}, {}), "foreign provider install bypassed owner");
  auto result = called.get_future();
  detail::engine_admission::notify_input(engine);
  const bool ready = result.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  const auto received = ready ? result.get() : clock::time_point{};
  engine.stop();
  check(ready && (finite && new_provider ? received != (clock::time_point::max)()
          && bounded_calls > 0 && legacy_calls == 0 : received == (clock::time_point::max)()
          && legacy_calls > 0 && bounded_calls == 0), "actual Engine selected wrong provider callback or dispatched both");
}

inline void run_reentrant_replacement(bool finite, bool replace) {
  input_deadline_cases::scoped_budget configuration(finite ? L"1000" : L"0");
  engine_thread engine;
  std::atomic<unsigned> retired{0}, new_calls{0};
  struct lifetime final {
    engine_thread& engine; std::atomic<unsigned>& retired; unsigned marker{47};
    lifetime(engine_thread& owner, std::atomic<unsigned>& count) : engine(owner), retired(count) {}
    ~lifetime() { (void)engine.state(); ++retired; }
  };
  auto held = std::make_shared<lifetime>(engine, retired);
  std::weak_ptr<lifetime> weak = held;
  std::promise<bool> called;
  auto finished = called.get_future();
  const auto change = [&] {
    return replace ? detail::engine_admission::install_owner_opportunity(engine, [&] { ++new_calls; },
        [&](clock::time_point) { ++new_calls; return false; }) : engine.set_owner_opportunity({});
  };
  engine_thread::task legacy_callback;
  std::function<bool(clock::time_point)> bounded_callback;
  if (finite) {
    legacy_callback = [] {};
    bounded_callback = [&, held](clock::time_point) {
      const bool changed = change();
      const bool alive = !weak.expired();
      called.set_value(changed && alive && held->marker == 47 && retired == 0);
      return true; // A replaced provider's stale continuation must not own a wake.
    };
  } else {
    legacy_callback = [&, held] {
      const bool changed = change();
      const bool alive = !weak.expired();
      called.set_value(changed && alive && held->marker == 47 && retired == 0);
    };
  }
  check(detail::engine_admission::install_owner_opportunity(engine, std::move(legacy_callback),
      std::move(bounded_callback)), "reentrant provider setup");
  held.reset();
  check(engine.start([] { return true; }), "reentrant provider startup");
  detail::engine_admission::notify_input(engine);
  const bool ready = finished.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  const bool retained_until_return = ready && finished.get();
  const bool barrier = engine.invoke([] {}) && engine.invoke([] {});
  engine.stop();
  check(retained_until_return && barrier && retired == 1 && weak.expired() && (!replace || new_calls > 0),
      "reentrant provider replacement destroyed active captures or restored the retired callback");
}

inline void run_engine() {
  run_configuration(L"0", false, true);
  run_configuration(L"invalid", false, true);
  run_configuration(L"99", false, true);
  run_configuration(L"1000", true, true);
  run_configuration(L"1000", true, false);
  run_reentrant_replacement(false, false);
  run_reentrant_replacement(false, true);
  run_reentrant_replacement(true, false);
  run_reentrant_replacement(true, true);
  {
    input_deadline_cases::scoped_budget configuration(L"1000");
    engine_thread engine;
    std::atomic<unsigned> calls{0};
    std::promise<void> resumed;
    check(detail::engine_admission::install_owner_opportunity(engine, {}, [&](clock::time_point) {
      const auto count = ++calls;
      if (count == 3) resumed.set_value();
      return count < 3;
    }), "provider continuation install");
    auto complete = resumed.get_future();
    check(engine.start([] { return true; }), "provider continuation startup");
    detail::engine_admission::notify_input(engine);
    const bool ready = complete.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    engine.stop();
    check(ready && calls >= 3, "actual Engine lost provider continuation at infinite wait");
  }
  {
    input_deadline_cases::scoped_budget configuration(L"1000");
    engine_thread engine;
    std::atomic<unsigned> retired{0}, bounded_calls{0};
    struct retirement final {
      engine_thread& engine; std::atomic<unsigned>& retired;
      retirement(engine_thread& owner, std::atomic<unsigned>& count) : engine(owner), retired(count) {}
      ~retirement() { (void)engine.state(); ++retired; }
    };
    auto held = std::make_shared<retirement>(engine, retired);
    check(detail::engine_admission::install_owner_opportunity(engine, [held] {},
        [&, held](clock::time_point) { ++bounded_calls; return false; }), "provider retirement cold install");
    held.reset();
    check(engine.start([] { return true; }), "provider retirement startup");
    bool cleared = false;
    check(engine.invoke([&] { cleared = engine.set_owner_opportunity([] {}); }), "provider old setter invocation");
    const auto at_clear = bounded_calls.load();
    check(engine.invoke([] {}), "provider old setter barrier");
    engine.stop();
    check(cleared && retired == 1 && bounded_calls == at_clear,
        "old provider setter retained bounded callback or retired its captures under lock");
  }
  {
    input_deadline_cases::scoped_budget configuration(L"1000");
    engine_thread engine;
    std::atomic<unsigned> retired{0};
    struct retirement final {
      engine_thread& engine; std::atomic<unsigned>& retired;
      retirement(engine_thread& owner, std::atomic<unsigned>& count) : engine(owner), retired(count) {}
      ~retirement() { (void)engine.state(); ++retired; }
    };
    auto first = std::make_shared<retirement>(engine, retired);
    auto second = std::make_shared<retirement>(engine, retired);
    check(detail::engine_admission::install_owner_opportunity(engine, [first] {},
        [second](clock::time_point) { return false; }), "dual provider retirement setup");
    first.reset(); second.reset();
    bool cleared_at_shutdown = false;
    check(engine.start([] { return true; }, [&] { cleared_at_shutdown = engine.set_owner_opportunity({}); }),
        "provider shutdown setup");
    bool replaced = false;
    check(engine.invoke([&] {
      replaced = detail::engine_admission::install_owner_opportunity(engine, [] {}, [](clock::time_point) { return false; });
    }), "provider dual replacement invocation");
    check(replaced && retired == 2, "provider dual replacement did not retire both captures outside lock");
    engine.stop();
    check(cleared_at_shutdown, "provider shutdown could not clear both callbacks on owner");
  }
}
#endif

inline void run() {
  run_bridge(); run_abi();
#ifdef _WIN32
  run_engine();
#endif
}
} // namespace provider_deadline_cases

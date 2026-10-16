#include <json.hpp>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <crtdbg.h>
#endif

namespace probe {
std::atomic<long long> outstanding{0};
thread_local bool forbidden = false;
thread_local std::size_t attempts = 0;
}

void* operator new(std::size_t size) {
  if (probe::forbidden) {
    ++probe::attempts;
    throw std::bad_alloc{};
  }
  if (void* p = std::malloc(size ? size : 1)) {
    ++probe::outstanding;
    return p;
  }
  throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept {
  if (p) { --probe::outstanding; std::free(p); }
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

template<class Json, class Builder>
bool destroy_without_allocation(const char* name, Builder builder) {
  const auto before = probe::outstanding.load();
  probe::attempts = 0;
  {
    Json value = builder();
    probe::forbidden = true;
  }
  probe::forbidden = false;
  const auto leaked = probe::outstanding.load() - before;
  const bool passed = probe::attempts == 0 && leaked == 0;
  std::printf("%s: allocations=%zu outstanding-delta=%lld %s\n",
      name, probe::attempts, leaked, passed ? "PASS" : "FAIL");
  return passed;
}

template<class Json>
Json nested(bool object, bool mixed) {
  Json root = object ? Json::object() : Json::array();
  Json* cursor = &root;
  for (std::size_t depth = 0; depth < 100000; ++depth) {
    const bool next_object = mixed ? depth % 2 == 0 : object;
    Json child = next_object ? Json::object() : Json::array();
    if (cursor->is_array()) {
      cursor->push_back(std::move(child));
      cursor = &cursor->back();
    } else {
      (*cursor)["child"] = std::move(child);
      cursor = &(*cursor)["child"];
    }
  }
  return root;
}

template<class Json>
bool run_suite(const char* kind) {
  std::printf("JSON type: %s\n", kind);
  bool ok = destroy_without_allocation<Json>("nested-scalars", [] {
    return Json{{"array", Json::array({1, true, "payload", nullptr})},
                {"object", Json{{"string", std::string(256, 's')}}},
                {"binary", Json::binary({0, 1, 2, 255})}};
  });
  ok = destroy_without_allocation<Json>("wide-mixed", [] {
    Json value = Json::array();
    for (int i = 0; i < 4096; ++i)
      value.push_back(Json{{"a", Json::array({i, "text"})}, {"b", Json::object()}});
    return value;
  }) && ok;
  ok = destroy_without_allocation<Json>("100000-arrays", [] { return nested<Json>(false, false); }) && ok;
  ok = destroy_without_allocation<Json>("100000-objects", [] { return nested<Json>(true, false); }) && ok;
  ok = destroy_without_allocation<Json>("100000-mixed", [] { return nested<Json>(false, true); }) && ok;
  const auto before = probe::outstanding.load();
  probe::attempts = 0;
  try {
    Json value{{"nested", Json::array({Json{{"x", "value"}}})}};
    probe::forbidden = true;
    throw 7;
  } catch (int) {
    probe::forbidden = false;
  }
  const bool unwinding = probe::attempts == 0 && probe::outstanding.load() == before;
  std::printf("exception-unwind: %s\n", unwinding ? "PASS" : "FAIL");
  return ok && unwinding;
}

int main() {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
  const bool regular = run_suite<nlohmann::json>("json");
  const bool ordered = run_suite<nlohmann::ordered_json>("ordered_json");
  return regular && ordered ? 0 : 1;
}

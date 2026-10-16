#include <souistd.h>
#include <core/SObjectFactory.h>
#include <event/NotifyCenter.h>
#include <layout/SLayoutSize.h>
#include <cstdio>
#include <memory>

namespace {
template<class T> struct SingletonData : SOUI::SSingleton<T> {
    static T** address() { return &SOUI::SSingleton<T>::ms_Singleton; }
};
int live_objects = 0;
class Probe final : public SOUI::SObject {
public:
    Probe() { ++live_objects; }
    ~Probe() override { --live_objects; }
    static LPCWSTR GetClassName() { return L"dependency-probe"; }
    static int GetClassType() { return SOUI::None; }
    static LPCWSTR BaseClassName() { return L"object"; }
};
bool require(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "%s\n", message);
    return condition;
}
}

int main() {
    // These are original imported data symbols, not local replacement storage.
    if (!require(*SingletonData<SOUI::SApplication>::address() == nullptr &&
                 *SingletonData<SOUI::SNotifyCenter>::address() == nullptr,
                 "SOUI singleton imports must initially be empty")) return 1;
    struct SizeCase { float value; SOUI::SLayoutSize::Unit unit; LPCWSTR text; };
    const SizeCase cases[] = {{12.5f, SOUI::SLayoutSize::px, L"12.5px"},
                             {10.0f, SOUI::SLayoutSize::dp, L"10dp"},
                             {0.0f, SOUI::SLayoutSize::dip, L"0dip"},
                             {-1.25f, SOUI::SLayoutSize::sp, L"-1.25sp"}};
    for (const auto& item : cases) {
        const auto text = SOUI::SLayoutSize(item.value, item.unit).toString();
        if (!require(text.Compare(item.text) == 0, "SOUI size formatting crossed DLL ABI incorrectly")) return 1;
    }
    SOUI::SObjectFactoryMgr factories;
    if (!require(factories.TplRegisterFactory<Probe>(), "Initial factory registration failed") ||
        !require(!factories.TplRegisterFactory<Probe>(), "Duplicate factory was accepted")) return 1;
    const SOUI::SObjectInfo type(Probe::GetClassName(), Probe::GetClassType());
    {
        // Registration's local factory has already gone out of scope.
        std::unique_ptr<SOUI::IObject> object(factories.CreateObject(type));
        if (!require(object != nullptr && live_objects == 1, "Registered factory did not retain its clone")) return 1;
    }
    if (!require(live_objects == 0, "Factory-created object did not release") ||
        !require(factories.UnregisterFactory(type), "Factory unregister failed") ||
        !require(!factories.UnregisterFactory(type), "Factory unregister repeated successfully")) return 1;
    std::puts("SOUI imported singleton, size formatting and factory ownership passed");
    return 0;
}

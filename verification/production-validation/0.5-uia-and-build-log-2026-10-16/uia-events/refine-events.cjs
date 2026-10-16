'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');const work=__dirname;
const provider=work+'/proposal/sdk/accessibility/uia/windows_uia_provider.cpp';
let cpp=fs.readFileSync(work+'/provider-before-trace.cpp','utf8');
cpp=cpp.replace('const auto& work = batch.items[batch.cursor];\n    element* target{};',`const auto& work = batch.items[batch.cursor];
    if (work.after) {
      std::lock_guard<std::mutex> held(shared->lock);
      const auto* current = find(*shared->view, work.after->node.id);
      if (!current || current->incarnation != work.after->incarnation) { ++batch.cursor; continue; }
    }
    element* target{};`);
fs.writeFileSync(provider,cpp);
const test=work+'/proposal/tests/native/windows_uia_provider_tests.cpp';let s=fs.readFileSync(test,'utf8');
// Keep precise event diagnostics in the native regression: one platform event
// can be delivered to a subtree subscriber more than once. diagnostic2 proves
// a single provider raise produced two valid callbacks; exact delivery count
// is not the provider contract. Validate every payload, then observe quiescence.
s=s.replace('std::atomic<unsigned> count{};\n  std::atomic<bool> failed{};', 'std::atomic<unsigned> count{};\n  std::atomic<ULONGLONG> last_event{};\n  std::atomic<bool> failed{};');
s=s.replace('VariantClear(&current); ++count; SetEvent(received.get()); return S_OK;', 'VariantClear(&current); last_event = GetTickCount64(); ++count; SetEvent(received.get()); return S_OK;');
s=s.replace('const auto removal = client->RemovePropertyChangedEventHandler(root.get(), sink.get());',`const auto deadline = GetTickCount64() + 1500;
    while (sink->count && GetTickCount64() - sink->last_event.load() < 150 && GetTickCount64() < deadline) Sleep(5);
    const auto delivered = sink->count.load();
    DWORD_PTR unchanged{};
    const bool republished = SendMessageTimeoutW(host.window, WM_APP + 0x478, 0, 0, SMTO_ABORTIFHUNG, 2000, &unchanged) != 0;
    Sleep(200); // Owner keeps pumping; a no-op publication must not emit another value change.
    const auto final_count = sink->count.load();
    const auto removal = client->RemovePropertyChangedEventHandler(root.get(), sink.get());
    require(republished && static_cast<HRESULT>(unchanged) == S_OK && final_count == delivered, "unchanged publication emits no new property event");`);
s=s.replace('sink->count == 1 && !sink->failed','sink->count >= 1 && !sink->failed');
s=s.replace('require(publications == 1, "actual native owner publication completed");','require(publications == 2, "changed and unchanged native publications completed");');
fs.writeFileSync(test,s);

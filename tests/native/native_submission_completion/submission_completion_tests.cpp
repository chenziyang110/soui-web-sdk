#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

// A deterministic native-submission contract model. The method under test is
// extracted byte-for-byte from the production translation unit by run.py.
// Deferred fake GDI execution is intentional; it does not reproduce Windows
// driver behavior or establish the cause of a historical desktop CRC failure.
using BOOL = int;
constexpr BOOL FALSE = 0, TRUE = 1;
using BYTE = unsigned char;
using DWORD = unsigned long;
constexpr DWORD SRCCOPY = 0x00cc0020;
constexpr BYTE AC_SRC_OVER = 0, AC_SRC_ALPHA = 1;
struct RECT { long left, top, right, bottom; };
struct BLENDFUNCTION { BYTE BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; };
struct SkMatrix { bool identity = false; void reset() { identity = true; } };
struct pixels { std::array<std::uint32_t, 64> values{}; };
using HDC = pixels*;
#define SkASSERT(value) do { if (!(value)) throw std::runtime_error("SkASSERT"); } while (false)

namespace model {
struct copy { HDC destination, source; };
std::vector<std::string> calls;
std::vector<copy> pending;
bool draw_ok = true, flush_ok = true;
void reset() { calls.clear(); pending.clear(); draw_ok = flush_ok = true; }
BOOL draw(const char* name, HDC destination, HDC source) {
    calls.emplace_back(name);
    if (draw_ok) pending.push_back({destination, source});
    return draw_ok;
}
}
BOOL BitBlt(HDC destination, int, int, int, int, HDC source, int, int, DWORD) {
    return model::draw("BitBlt", destination, source);
}
BOOL GdiAlphaBlend(HDC destination, int, int, int, int, HDC source, int, int,
                   int, int, BLENDFUNCTION) {
    return model::draw("GdiAlphaBlend", destination, source);
}
BOOL GdiFlush() {
    model::calls.emplace_back("GdiFlush");
    for (const auto& operation : model::pending)
        operation.destination->values = operation.source->values;
    model::pending.clear();
    return model::flush_ok;
}
class BitmapPlatformDevice {
public:
    BitmapPlatformDevice(pixels& source, bool opaque, bool cached)
        : source_(&source), opaque_(opaque), cached_(cached) { }
    bool IsBitmapDCCreated() const { return cached_; }
    HDC BeginPlatformPaint(void*) { model::calls.emplace_back("BeginPlatformPaint"); return source_; }
    void EndPlatformPaint() { model::calls.emplace_back("EndPlatformPaint"); }
    void ReleaseBitmapDC() { model::calls.emplace_back("ReleaseBitmapDC"); }
    void LoadTransformToDC(HDC, const SkMatrix& transform) {
        model::calls.emplace_back(transform.identity ? "TransformIdentity" : "TransformRestore");
    }
    int width() const { return 8; }
    int height() const { return 8; }
    bool isOpaque() const { return opaque_; }
    bool DrawToNativeContext(HDC dc, int x, int y, const RECT* src_rect);
    SkMatrix transform_;
private:
    HDC source_;
    bool opaque_, cached_;
};

#include "method-under-test.inc"

namespace {
int failures = 0, checks = 0;
void expect(bool condition, const std::string& name, const char* rule) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL %s: %s\n", name.c_str(), rule); }
}
size_t position(const char* name) {
    return static_cast<size_t>(std::find(model::calls.begin(), model::calls.end(), name) - model::calls.begin());
}
void run(bool opaque, bool cached, bool draw_ok, bool flush_ok, bool explicit_rect) {
    const std::string name = std::string(opaque ? "opaque" : "alpha") +
        (cached ? "-cached" : "-new") + (draw_ok ? "-draw-ok" : "-draw-fail") +
        (flush_ok ? "-flush-ok" : "-flush-fail") + (explicit_rect ? "-rect" : "-full");
    model::reset(); model::draw_ok = draw_ok; model::flush_ok = flush_ok;
    pixels source, destination, preceding_source, preceding_destination;
    source.values.fill(0x123456);
    preceding_source.values.fill(0xabcdef);
    // A failed draw must still drain previously queued issuing-thread work.
    if (!draw_ok) model::pending.push_back({&preceding_destination, &preceding_source});
    BitmapPlatformDevice device(source, opaque, cached);
    const RECT rect{1, 1, 7, 7};
    const bool accepted = device.DrawToNativeContext(&destination, 0, 0, explicit_rect ? &rect : nullptr);
    expect(accepted == (draw_ok && flush_ok), name, "result reflects draw and flush");
    expect(model::pending.empty(), name, "submission drained before caller reuses source");
    expect(std::count(model::calls.begin(), model::calls.end(), "GdiFlush") == 1, name, "flush executes exactly once even after draw failure");
    expect(position("TransformRestore") < position("GdiFlush") &&
           position("GdiFlush") < position("EndPlatformPaint"), name, "restore then flush then end paint");
    expect(std::count(model::calls.begin(), model::calls.end(), "EndPlatformPaint") == 1, name, "end paint cleanup runs");
    expect(std::count(model::calls.begin(), model::calls.end(), "ReleaseBitmapDC") == (cached ? 0 : 1), name, "only newly created DC released");
    if (!cached) expect(position("GdiFlush") < position("ReleaseBitmapDC"), name, "source DC retained until flush");
    expect(std::count(model::calls.begin(), model::calls.end(), opaque ? "BitBlt" : "GdiAlphaBlend") == 1, name, "correct native draw path");
    // Mutation occurs immediately on return, followed by any outstanding fake
    // batch. This makes the source-lifetime regression deterministic.
    source.values.fill(0x654321);
    if (!model::pending.empty()) GdiFlush();
    if (draw_ok) expect(destination.values.front() == 0x123456, name, "destination observes pre-reuse source pixels");
    else expect(preceding_destination.values.front() == 0xabcdef, name, "failed draw still drains preceding batch");
}
}
int main() {
    for (bool opaque : {true, false}) for (bool cached : {true, false})
        for (bool draw_ok : {true, false}) for (bool flush_ok : {true, false})
            for (bool explicit_rect : {true, false}) run(opaque, cached, draw_ok, flush_ok, explicit_rect);
    std::printf("{\"schema\":\"native-submission-completion-tests/v1\",\"cases\":32,\"checks\":%d,\"failures\":%d,\"fakeGdi\":true,\"historicalCrcCauseProven\":false}\n", checks, failures);
    return failures ? 1 : 0;
}

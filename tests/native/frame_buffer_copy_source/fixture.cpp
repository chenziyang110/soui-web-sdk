// Compile complete production methods. Skia, allocation and trace boundaries are
// deterministic fakes; this does not create a native canvas, host or window.
#include "cc/trees/FrameBufferLeasePolicy.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace fixture {
bool failNextAllocation = false;
enum class AllocationFailure { none, nullCanvas, canvasBadAlloc, frameBufferBadAlloc };
AllocationFailure allocationFailure = AllocationFailure::none;
struct Counters {
    uint64_t clearCalls = 0, clearBytes = 0, drawCalls = 0, drawBytes = 0;
    uint64_t allocationRequests = 0, allocatedCanvases = 0, destroyedCanvases = 0;
    uint64_t copyOperations = 0, copyBytes = 0;
};
Counters counters;
int failures = 0;
const char* currentCase = "setup";
void require(bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL [%s] %s\n", currentCase, message);
    }
}
void equal(uint64_t actual, uint64_t expected, const char* message)
{
    if (actual != expected) {
        ++failures;
        std::fprintf(stderr, "FAIL [%s] %s: expected %llu, observed %llu\n", currentCase,
            message, static_cast<unsigned long long>(expected), static_cast<unsigned long long>(actual));
    }
}
// Premultiplied RGBA, with alpha in the high byte. Rounded integer SrcOver
// preserves transparent source pixels and exposes stale destination contents.
uint32_t sourceOver(uint32_t source, uint32_t destination)
{
    const uint32_t inverseAlpha = 255 - (source >> 24);
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const uint32_t sourceChannel = (source >> shift) & 255;
        const uint32_t destinationChannel = (destination >> shift) & 255;
        const uint32_t blended = sourceChannel + (destinationChannel * inverseAlpha + 127) / 255;
        result |= std::min(blended, 255U) << shift;
    }
    return result;
}
}

// Fail the actual std::make_shared<FrameBuffer> allocation after the fake
// platform has returned a canvas. The one-shot flag is never armed by asserts.
void* operator new(std::size_t bytes)
{
    if (fixture::failNextAllocation) {
        fixture::failNextAllocation = false;
        throw std::bad_alloc();
    }
    if (void* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

struct SkRect {
    int left = 0, top = 0, right = 4, bottom = 1;
    int x() const { return left; }
    int y() const { return top; }
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    void set(int l, int t, int r, int b) { left = l; top = t; right = r; bottom = b; }
};
struct SkXfermode { enum Mode { kClear_Mode, kSrcOver_Mode }; };
struct SkPaint {
    uint32_t color = 0;
    SkXfermode::Mode mode = SkXfermode::kSrcOver_Mode;
    void setARGB(unsigned a, unsigned r, unsigned g, unsigned b)
    { color = (a << 24) | (r << 16) | (g << 8) | b; }
    void setXfermodeMode(SkXfermode::Mode value) { mode = value; }
};
struct SkBitmap {
    int width, height;
    std::vector<uint32_t> pixels;
    SkBitmap(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * h, 0xC0403020) { }
};
struct SkBaseDevice {
    SkBitmap bitmap;
    SkBaseDevice(int w, int h) : bitmap(w, h) { }
    const SkBitmap& accessBitmap(bool) const { return bitmap; }
};
struct SkCanvas {
    SkBaseDevice device;
    bool opaque;
    SkCanvas(int w, int h, bool isOpaque) : device(w, h), opaque(isOpaque)
    { ++fixture::counters.allocatedCanvases; }
    ~SkCanvas() { ++fixture::counters.destroyedCanvases; }
    void drawRect(const SkRect& rect, const SkPaint& paint)
    {
        ++fixture::counters.clearCalls;
        for (int y = std::max(0, rect.y()); y < std::min(device.bitmap.height, rect.bottom); ++y) {
            for (int x = std::max(0, rect.x()); x < std::min(device.bitmap.width, rect.right); ++x) {
                auto& destination = device.bitmap.pixels[static_cast<size_t>(y) * device.bitmap.width + x];
                destination = paint.mode == SkXfermode::kClear_Mode ? 0 : fixture::sourceOver(paint.color, destination);
                fixture::counters.clearBytes += 4;
            }
        }
    }
    void drawBitmap(const SkBitmap& bitmap, int x, int y, const SkPaint*)
    {
        fixture::require(x == 0 && y == 0, "production copy uses the bitmap origin");
        ++fixture::counters.drawCalls;
        for (int row = 0; row < std::min(bitmap.height, device.bitmap.height); ++row) {
            for (int column = 0; column < std::min(bitmap.width, device.bitmap.width); ++column) {
                auto& destination = device.bitmap.pixels[static_cast<size_t>(row) * device.bitmap.width + column];
                const uint32_t source = bitmap.pixels[static_cast<size_t>(row) * bitmap.width + column];
                destination = fixture::sourceOver(source, destination);
                fixture::counters.drawBytes += 4;
            }
        }
    }
};
namespace skia {
SkCanvas* CreatePlatformCanvas(int width, int height, bool opaque)
{
    ++fixture::counters.allocationRequests;
    if (fixture::allocationFailure == fixture::AllocationFailure::nullCanvas) return nullptr;
    if (fixture::allocationFailure == fixture::AllocationFailure::canvasBadAlloc) throw std::bad_alloc();
    auto* canvas = new SkCanvas(width, height, opaque);
    if (fixture::allocationFailure == fixture::AllocationFailure::frameBufferBadAlloc)
        fixture::failNextAllocation = true;
    return canvas;
}
SkBaseDevice* GetTopDevice(SkCanvas& canvas) { return &canvas.device; }
}

namespace cc {
namespace trace {
enum class counter { copy_operations, copy_bytes };
void add(counter value, uint64_t amount = 1)
{
    if (value == counter::copy_operations) fixture::counters.copyOperations += amount;
    else fixture::counters.copyBytes += amount;
}
}
// Only the shell is modeled. Ownership and decisions execute the source method
// below, including shared_ptr::use_count(), the real policy and constructor.
class LayerTreeHost {
public:
    struct FrameBuffer {
        FrameBuffer(std::unique_ptr<SkCanvas>, uint64_t, int, int, bool);
        ~FrameBuffer();
        std::unique_ptr<SkCanvas> canvas;
        uint64_t generation, bytes;
        int width, height;
        bool alpha;
    };
    SkRect m_clientRect;
    bool m_hasResize = false, m_hasTransparentBackground = true;
    uint64_t m_frameBufferGeneration = 1;
    size_t m_currentFrameBufferSlot = 0;
    std::shared_ptr<FrameBuffer> m_frameBuffers[FrameBufferLeasePolicy::maximumSlots];
    SkCanvas* m_memoryCanvas = nullptr;
    void clearCanvas(SkCanvas*, const SkRect&, bool);
    bool prepareWritableFrameBufferLocked();
};
#include "frame_buffer_constructor.inc"
#include "clear_canvas.inc"
#include "prepare_writable_frame_buffer.inc"
}

namespace fixture {
using Host = cc::LayerTreeHost;
void resetCounters() { counters = Counters{}; }
void seed(Host& host)
{
    require(host.prepareWritableFrameBufferLocked(), "initial slot allocation succeeds");
}
std::vector<uint32_t>& pixels(Host& host) { return host.m_memoryCanvas->device.bitmap.pixels; }
const std::vector<uint32_t> translucent = { 0, 0x80602010, 0xFF315375, 0x40102030 };
const std::vector<uint32_t> translucentNext = { 0x40201008, 0, 0x80402010, 0xFF224466 };
const std::vector<uint32_t> opaque = { 0xFF102030, 0xFF405060, 0xFF708090, 0xFFA0B0C0 };
void expectCopy(uint64_t clears, uint64_t allocations)
{
    equal(counters.clearCalls, clears, "clear calls for selected writable slot");
    equal(counters.clearBytes, clears * 16, "clear bytes");
    equal(counters.allocationRequests, allocations, "platform allocation requests");
    equal(counters.drawCalls, 1, "previous complete bitmap is copied once");
    equal(counters.drawBytes, 16, "actual bitmap bytes drawn");
    equal(counters.copyOperations, 1, "production copy operation counter");
    equal(counters.copyBytes, 16, "production copy byte counter");
}
void transparentNew()
{
    Host host;
    seed(host);
    pixels(host) = translucent;
    auto lease = host.m_frameBuffers[0];
    auto* previous = host.m_memoryCanvas;
    resetCounters();
    require(host.prepareWritableFrameBufferLocked(), "leased current slot selects a new spare");
    if (counters.clearCalls != 1)
        std::fprintf(stderr, "new-transparent-clear-count expected=1 observed=%llu\n",
            static_cast<unsigned long long>(counters.clearCalls));
    expectCopy(1, 1); // RED before fix: new transparent spare is cleared twice.
    require(pixels(host) == translucent, "full premultiplied alpha pixels equal previous frame");
    require(lease->canvas.get() == previous && lease->canvas->device.bitmap.pixels == translucent,
        "old lease keeps its unchanged canvas and pixels");
    equal(host.m_currentFrameBufferSlot, 1, "new spare is current");
    equal(host.m_frameBuffers[1]->generation, 1, "copy preserves generation");
    require(!host.m_memoryCanvas->opaque, "transparent allocation requests alpha");
}
void transparentReuse()
{
    Host host;
    seed(host);
    auto firstLease = host.m_frameBuffers[0];
    require(host.prepareWritableFrameBufferLocked(), "setup allocates second slot");
    pixels(host) = translucentNext;
    firstLease.reset();
    auto currentLease = host.m_frameBuffers[1];
    host.m_frameBuffers[0]->canvas->device.bitmap.pixels = opaque;
    resetCounters();
    require(host.prepareWritableFrameBufferLocked(), "released old slot is reusable");
    expectCopy(1, 0);
    require(pixels(host) == translucentNext, "reused stale slot is cleared before transparent SrcOver");
    require(currentLease->canvas->device.bitmap.pixels == translucentNext, "current lease is unchanged");
    equal(host.m_currentFrameBufferSlot, 0, "reused slot is current");
}
void opaqueCopy()
{
    Host host;
    host.m_hasTransparentBackground = false;
    seed(host);
    pixels(host) = opaque;
    auto firstLease = host.m_frameBuffers[0];
    resetCounters();
    require(host.prepareWritableFrameBufferLocked(), "opaque current lease selects new spare");
    expectCopy(1, 1);
    require(pixels(host) == opaque && host.m_memoryCanvas->opaque, "opaque new slot pixels and allocation");
    firstLease.reset();
    auto currentLease = host.m_frameBuffers[1];
    host.m_frameBuffers[0]->canvas->device.bitmap.pixels.assign(4, 0xC0403020);
    resetCounters();
    require(host.prepareWritableFrameBufferLocked(), "opaque old slot is reused");
    expectCopy(0, 0);
    require(pixels(host) == opaque, "opaque full copy replaces stale slot pixels");
}
void leasesExhausted()
{
    Host host;
    seed(host);
    auto firstLease = host.m_frameBuffers[0];
    require(host.prepareWritableFrameBufferLocked(), "setup allocates second slot");
    auto secondLease = host.m_frameBuffers[1];
    auto* previous = host.m_memoryCanvas;
    resetCounters();
    require(!host.prepareWritableFrameBufferLocked(), "two leased slots refuse a writable buffer");
    equal(counters.allocationRequests + counters.clearCalls + counters.drawCalls, 0, "exhaustion performs no canvas work");
    require(host.m_memoryCanvas == previous && host.m_currentFrameBufferSlot == 1,
        "exhaustion leaves the current canvas and slot unchanged");
    firstLease.reset();
    require(host.prepareWritableFrameBufferLocked(), "released slot becomes writable");
    require(secondLease == host.m_frameBuffers[1], "other lease remains retained");
}
void sizeAlphaGeneration()
{
    Host host;
    seed(host);
    auto oldSizeLease = host.m_frameBuffers[0];
    host.m_clientRect.set(0, 0, 2, 2);
    host.m_hasResize = true;
    host.m_frameBufferGeneration = 2;
    resetCounters();
    require(host.prepareWritableFrameBufferLocked(), "resize allocates a fresh generation");
    equal(counters.clearCalls, 1, "resized transparent allocation clears once");
    equal(counters.drawCalls, 0, "old generation is not copied after resize");
    require(pixels(host) == std::vector<uint32_t>(4, 0), "new alpha generation begins transparent");
    require(oldSizeLease->width == 4 && oldSizeLease->height == 1 && oldSizeLease->generation == 1,
        "resize does not mutate leased dimensions or generation");
    equal(host.m_frameBuffers[1]->bytes, 16, "actual FrameBuffer constructor records bytes");
    require(!host.m_hasResize, "successful resize clears the resize flag");
    oldSizeLease.reset();
    auto alphaLease = host.m_frameBuffers[1];
    host.m_hasTransparentBackground = false;
    host.m_frameBufferGeneration = 3;
    host.m_hasResize = true;
    resetCounters();
    require(host.prepareWritableFrameBufferLocked(), "alpha change replaces incompatible slot");
    equal(counters.clearCalls, 1, "opaque replacement clears once");
    equal(counters.drawCalls, 0, "incompatible alpha generation is not copied");
    require(pixels(host) == std::vector<uint32_t>(4, 0xFFFFFFFF), "actual opaque clear uses white");
    require(alphaLease->alpha && alphaLease->generation == 2, "old alpha lease retains its generation");
    require(!host.m_frameBuffers[0]->alpha && host.m_frameBuffers[0]->generation == 3,
        "replacement records alpha and generation");
    host.m_hasResize = true;
    host.m_frameBufferGeneration = (std::numeric_limits<uint64_t>::max)();
    resetCounters();
    require(!host.prepareWritableFrameBufferLocked(), "resize rejects exhausted generation");
    equal(counters.allocationRequests, 0, "generation exhaustion allocates nothing");
    host.m_frameBufferGeneration = 4;
    host.m_clientRect.set(0, 0, 0, 2);
    require(!host.prepareWritableFrameBufferLocked(), "empty dimensions are rejected by actual byte policy");
    host.m_clientRect.set(0, 0, (std::numeric_limits<int>::max)(), (std::numeric_limits<int>::max)());
    require(!host.prepareWritableFrameBufferLocked(), "oversize dimensions are rejected by actual byte policy");
    equal(counters.allocationRequests, 0, "invalid dimensions never reach platform allocation");
}
void allocationFailures()
{
    for (auto failure : { AllocationFailure::nullCanvas, AllocationFailure::canvasBadAlloc,
            AllocationFailure::frameBufferBadAlloc }) {
        Host host;
        seed(host);
        pixels(host) = translucent;
        auto lease = host.m_frameBuffers[0];
        auto* previous = host.m_memoryCanvas;
        resetCounters();
        allocationFailure = failure;
        const bool prepared = host.prepareWritableFrameBufferLocked();
        allocationFailure = AllocationFailure::none;
        require(!prepared, "platform null/bad_alloc and FrameBuffer bad_alloc fail preparation");
        equal(counters.allocationRequests, 1, "failed spare attempts one platform allocation");
        equal(counters.drawCalls + counters.copyOperations + counters.copyBytes, 0, "failed allocation does not copy");
        require(!host.m_frameBuffers[1] && host.m_memoryCanvas == previous && host.m_currentFrameBufferSlot == 0,
            "failed spare keeps the current leased frame and empty spare");
        require(lease->canvas->device.bitmap.pixels == translucent, "allocation failure preserves leased pixels");
        const uint64_t allocated = failure == AllocationFailure::frameBufferBadAlloc ? 1 : 0;
        equal(counters.allocatedCanvases, allocated, "only metadata failure creates a canvas");
        equal(counters.destroyedCanvases, allocated, "metadata failure releases the temporary canvas");
        equal(counters.clearCalls, allocated, "only created canvas is cleared on failure");
        require(!failNextAllocation, "one-shot metadata allocation failure was consumed");
    }
    Host host;
    seed(host);
    host.m_frameBufferGeneration = 2;
    host.m_hasResize = true;
    allocationFailure = AllocationFailure::nullCanvas;
    const bool prepared = host.prepareWritableFrameBufferLocked();
    allocationFailure = AllocationFailure::none;
    require(!prepared && !host.m_memoryCanvas && !host.m_frameBuffers[0] && host.m_hasResize,
        "failed replacement of an unleased current slot leaves no dangling canvas");
}
}

int main(int argc, char** argv)
{
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {
        { "transparent-new", fixture::transparentNew },
        { "transparent-reuse", fixture::transparentReuse },
        { "opaque", fixture::opaqueCopy },
        { "leases-exhausted", fixture::leasesExhausted },
        { "size-alpha-generation", fixture::sizeAlphaGeneration },
        { "allocation-failure", fixture::allocationFailures }
    };
    const char* selected = nullptr;
    if (argc == 3 && std::strcmp(argv[1], "--case") == 0) selected = argv[2];
    else if (argc != 1) { std::fputs("usage: frame_buffer_copy_contract [--case name]\n", stderr); return 2; }
    unsigned executed = 0;
    for (const auto& sample : cases) {
        if (selected && std::strcmp(selected, sample.name) != 0) continue;
        fixture::currentCase = sample.name;
        fixture::resetCounters();
        sample.run();
        ++executed;
    }
    if (!executed) { std::fputs("unknown frame buffer copy case\n", stderr); return 2; }
    std::printf("frame-buffer-copy-source: %u cases, %d failures\n", executed, fixture::failures);
    return fixture::failures ? 1 : 0;
}

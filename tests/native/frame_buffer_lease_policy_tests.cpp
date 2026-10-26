#include "cc/trees/FrameBufferLeasePolicy.h"
#include "content/common/PaintLifetime.h"
#include <cstdio>
#include <memory>
#include <set>
#include <thread>

static int failures = 0;
static void require(bool value) { if (!value) { ++failures; std::fputs("framebuffer lease requirement failed\n", stderr); } }
int main()
{
    using Policy = cc::FrameBufferLeasePolicy;
    uint64_t bytes = 0;
    require(Policy::bytesForSize(3840, 2160, &bytes) && bytes == 33177600);
    require(!Policy::bytesForSize(0, 2160, &bytes));
    require(!Policy::bytesForSize(100000, 100000, &bytes));
    auto front = std::make_shared<int>(17);
    auto lease = front;
    auto back = std::make_shared<int>(23);
    require(Policy::writableSlot(0, front.use_count(), back.use_count()) == 1);
    auto secondLease = back;
    require(Policy::writableSlot(0, front.use_count(), back.use_count()) == -1);
    back.reset(); // Pool/host close cannot invalidate an outstanding lease.
    require(*lease == 17 && *secondLease == 23);
    secondLease.reset();
    lease.reset();
    require(Policy::writableSlot(0, front.use_count(), 0) == 0);
    cc::UiMirrorConsumerPolicy mirror;
    require(mirror.required(true, false)); // Legacy explicit-consumer default.
    mirror.setExplicitRequired(false);
    require(!mirror.required(true, false)); // Confirmed DIRECT_HWND with no DC consumer.
    require(mirror.required(false, false) && mirror.required(true, true));
    mirror.noteDcConsumer();
    require(mirror.required(true, false));
    mirror.setExplicitRequired(false);
    require(mirror.required(true, false)); // Later Host configuration cannot revoke a DC.

    using Kind = content::PaintCloseKind;
    auto paint = std::make_shared<content::PaintLifetimeState>();
    {
        content::PaintActivityScope outerDraw(paint);
        require(bool(outerDraw));
        {
            content::PaintActivityScope clientCallback(paint);
            require(bool(clientCallback));
            content::PaintClosePermit requestedClose(paint, Kind::view);
            require(!requestedClose && paint->policy.pending == Kind::view);
            require(paint->policy.active == 2); // Client and outer host stack both retained.
            content::PaintActivityScope newPaint(paint);
            require(!newPaint); // Closing seals future activity, not the live outer stack.
        }
        require(paint->policy.active == 1 && !paint->policy.ready());
    }
    require(paint->policy.ready());
    {
        content::PaintClosePermit actualClose(paint, Kind::view);
        require(bool(actualClose) && paint->policy.pending == Kind::none);
        content::PaintClosePermit duplicate(paint, Kind::view);
        require(!duplicate);
        content::PaintClosePermit windowCascade(paint, Kind::window);
        require(bool(windowCascade)); // NCDESTROY/view close may cascade once.
        require(content::paintLifetimeRuntime().callouts == 2);
    }
    require(content::paintLifetimeRuntime().drained());

    content::PaintLifetimePolicy deferred;
    require(deferred.enter() && !deferred.request(Kind::window));
    require(deferred.leave() && deferred.ready());
    const auto rejected = deferred.reserveTask();
    require(rejected && !deferred.reserveTask()); // One queued ticket per view.
    deferred.disposeTask(rejected); // Allocation/post rejection keeps the close intent.
    require(deferred.ready() && !deferred.queued);
    const auto replacement = deferred.reserveTask();
    deferred.disposeTask(rejected);
    require(deferred.queued == replacement); // Stale disposal cannot clear a newer receipt.
    deferred.disposeTask(replacement);
    require(deferred.request(Kind::window));
    require(deferred.request(Kind::view)); // Native window destruction may delete its view.
    deferred.invalidate();
    require(!deferred.enter() && !deferred.ready() && !deferred.reserveTask());
    deferred.disposeTask(replacement); // Tombstone has no target to dereference.
    content::PaintLifetimePolicy exhausted;
    exhausted.pending = Kind::view; exhausted.serial = (std::numeric_limits<uint64_t>::max)();
    require(!exhausted.reserveTask() && exhausted.ready()); // Owner service remains possible.

    content::PaintLifetimeRuntime shutdown;
    require(shutdown.drained());
    shutdown.active = 1; require(!shutdown.drained()); shutdown.active = 0;
    shutdown.pending = 1; require(!shutdown.drained()); shutdown.pending = 0;
    shutdown.queued = 1; require(!shutdown.drained()); shutdown.queued = 0;
    shutdown.callouts = 1; require(!shutdown.drained()); shutdown.callouts = 0;
    require(shutdown.drained());
    content::paintLifetimeRuntime().closing = true;
    auto afterShutdown = std::make_shared<content::PaintLifetimeState>();
    require(!content::PaintActivityScope(afterShutdown));
    content::paintLifetimeRuntime().closing = false;

    content::PaintCloseServiceCursor<unsigned> cursor;
    require(cursor.enterService() && !cursor.enterService());
    cursor.leaveService();
    require(cursor.enterService());
    cursor.leaveService();
    std::set<unsigned> views;
    for (unsigned value = 1; value <= 40; ++value) views.insert(value);
    for (unsigned count = 1; count <= 32; ++count) {
        const auto selected = cursor.next(views);
        require(selected == count);
        views.erase(selected); // Deletion invalidates no retained iterator or object ID.
    }
    views.insert(1); // Reused address is a fresh lookup, not an old queued target.
    for (unsigned count = 33; count <= 40; ++count) require(cursor.next(views) == count);
    require(cursor.next(views) == 1); // Fair wrap across bounded owner calls.

    // BitInfo-style foreign lifetime: pixel deletion and completion publication
    // both finish while the activity pin still holds owner/module close pending.
    auto heldState = std::make_shared<content::PaintLifetimeState>();
    std::atomic<bool> pixelsReleasedWhilePinned{false}, notifiedWhilePinned{false};
    struct HeldBitInfoSource {
        content::PaintActivityScope activity; // Destroyed last, as in FrameBufferLease.
        std::shared_ptr<int> pixels;
        std::shared_ptr<content::PaintLifetimeState> state;
        std::atomic<bool>& notified;
        HeldBitInfoSource(const std::shared_ptr<content::PaintLifetimeState>& value,
            std::atomic<bool>& deleted, std::atomic<bool>& notification)
            : activity(value), pixels(new int(1), [&deleted](int* pixel) {
                auto& runtime = content::paintLifetimeRuntime();
                std::lock_guard<std::mutex> lock(runtime.mutex);
                deleted.store(runtime.active != 0);
                delete pixel;
            }), state(value), notified(notification) { }
        ~HeldBitInfoSource()
        {
            pixels.reset();
            auto& runtime = content::paintLifetimeRuntime();
            std::lock_guard<std::mutex> lock(runtime.mutex);
            notified.store(runtime.active != 0 && state->policy.active != 0
                && state->policy.pending == Kind::view);
        }
    };
    auto heldBitInfo = std::make_unique<HeldBitInfoSource>(heldState,
        pixelsReleasedWhilePinned, notifiedWhilePinned);
    {
        content::PaintClosePermit ownerClose(heldState, Kind::view);
        require(!ownerClose && !heldState->policy.ready());
    }
    require(!content::paintLifetimeRuntime().drained());
    std::thread foreignRelease([held = std::move(heldBitInfo)]() mutable { held.reset(); });
    foreignRelease.join();
    require(pixelsReleasedWhilePinned && notifiedWhilePinned && heldState->policy.ready());
    {
        content::PaintClosePermit ownerClose(heldState, Kind::view);
        require(bool(ownerClose));
        heldState->policy.invalidate(); // Actual close may delete the old target now.
        require(!content::paintLifetimeRuntime().drained()); // Close callout still on stack.
    }
    require(content::paintLifetimeRuntime().drained());
    return failures ? 1 : 0;
}

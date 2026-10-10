#pragma once
#include <atomic>
#include <cstdlib>
#include <new>

namespace allocation_fault {
std::atomic<long> countdown(-1);
std::atomic<long> attempts(0);
std::atomic<long> failures(0);
void* tracked[256] = {};
int deleted[256] = {};
std::atomic<std::size_t> capture_size(0);
std::atomic<void*> captured(nullptr);
std::atomic<int> captured_deleted(0);

bool fail_now()
{
    long value = countdown.load();
    while (value >= 0) {
        const long next = value == 0 ? -1 : value - 1;
        if (countdown.compare_exchange_weak(value, next)) {
            ++attempts;
            if (value == 0) { ++failures; return true; }
            return false;
        }
    }
    return false;
}

void arm(long permitted_allocations)
{
    attempts = 0;
    failures = 0;
    countdown = permitted_allocations;
}

void disarm() { countdown = -1; }

void track(void* pointer, int index)
{
    tracked[index] = pointer;
    deleted[index] = 0;
}

void note_delete(void* pointer)
{
    if (!pointer) return;
    if (captured.load() == pointer) ++captured_deleted;
    // Tracking is only mutated in the single-thread endpoint fixture; the
    // activation fixture leaves these arrays empty for its entire lifetime.
    for (int i = 0; i != 256; ++i) if (tracked[i] == pointer) ++deleted[i];
}
}

void* operator new(std::size_t size)
{
    if (allocation_fault::fail_now()) throw std::bad_alloc();
    void* result = std::malloc(size ? size : 1);
    if (!result) throw std::bad_alloc();
    if (allocation_fault::capture_size.load() == size) allocation_fault::captured = result;
    return result;
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { allocation_fault::note_delete(pointer); std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

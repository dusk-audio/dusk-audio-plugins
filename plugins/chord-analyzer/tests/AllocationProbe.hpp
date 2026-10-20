// Test-only C++ allocation interception. Include from one translation unit per executable.
#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>

namespace ChordAllocationProbe
{
inline thread_local bool watchAllocations = false;
inline thread_local std::size_t allocationCalls = 0;
inline thread_local bool insideNew = false;
inline void* allocate(std::size_t bytes)
{
    if (watchAllocations) ++allocationCalls;
    insideNew = true;
    void* memory = std::malloc(bytes == 0 ? 1 : bytes);
    insideNew = false;
    if (memory) return memory;
    throw std::bad_alloc();
}
inline bool positiveControl()
{
    allocationCalls = 0;
    watchAllocations = true;
    void* (*volatile request)(std::size_t) = ::operator new;
    void* memory = request(8);
    ::operator delete(memory);
    void* (*volatile requestArray)(std::size_t) = ::operator new[];
    memory = requestArray(8);
    ::operator delete[](memory);
    void* (*volatile requestNoThrow)(std::size_t, const std::nothrow_t&) = ::operator new;
    memory = requestNoThrow(8, std::nothrow);
    ::operator delete(memory);
    void* (*volatile requestArrayNoThrow)(std::size_t, const std::nothrow_t&) = ::operator new[];
    memory = requestArrayNoThrow(8, std::nothrow);
    ::operator delete[](memory);
    watchAllocations = false;
    const bool observed = allocationCalls == 4;
    allocationCalls = 0;
    return observed;
}
}

void* operator new(std::size_t bytes) { return ChordAllocationProbe::allocate(bytes); }
void* operator new[](std::size_t bytes) { return ChordAllocationProbe::allocate(bytes); }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{
    try { return ChordAllocationProbe::allocate(bytes); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{
    try { return ChordAllocationProbe::allocate(bytes); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

#if defined(CHORD_TEST_WRAP_MALLOC)
// Linux link-time interception also covers JUCE's direct HeapBlock allocation,
// which a C++ new-only probe misses (e.g. copying a long SysEx message).
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void* __wrap_malloc(std::size_t size)
{
    if (ChordAllocationProbe::watchAllocations && !ChordAllocationProbe::insideNew)
        ++ChordAllocationProbe::allocationCalls;
    return __real_malloc(size);
}
extern "C" void* __wrap_calloc(std::size_t count, std::size_t size)
{
    if (ChordAllocationProbe::watchAllocations && !ChordAllocationProbe::insideNew)
        ++ChordAllocationProbe::allocationCalls;
    return __real_calloc(count, size);
}
extern "C" void* __wrap_realloc(void* old, std::size_t size)
{
    if (ChordAllocationProbe::watchAllocations && !ChordAllocationProbe::insideNew)
        ++ChordAllocationProbe::allocationCalls;
    return __real_realloc(old, size);
}
#endif

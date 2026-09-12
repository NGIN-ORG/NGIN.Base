#include "AllocationInstrumentation.hpp"

#include <NGIN/Memory/SystemAllocator.hpp>

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <new>

namespace
{
    // Constant initialization and trivial destruction permit replacement new to
    // be used by static initializers and late process teardown as well.
    constinit std::atomic<std::size_t> liveBytes {}, peakBytes {}, totalBytes {}, liveCount {}, totalCount {};

    struct Header
    {
        void*       base;
        std::size_t requested;
        std::size_t allocated;
    };

    void* Allocate(std::size_t size, std::size_t alignment)
    {
        const std::size_t payload = std::max(size, std::size_t {1});
        alignment                 = std::max(alignment, alignof(Header));
        const std::size_t limit   = std::numeric_limits<std::size_t>::max();
        if ((alignment & (alignment - 1)) != 0 || alignment - 1 > limit - sizeof(Header) ||
            payload > limit - sizeof(Header) - (alignment - 1))
            throw std::bad_alloc();
        const std::size_t allocated = payload + sizeof(Header) + alignment - 1;
        void*             base;
        for (;;)
        {
            base = NGIN::Memory::SystemAllocator {}.Allocate(allocated, alignof(std::max_align_t));
            if (base)
                break;
            const std::new_handler handler = std::get_new_handler();
            if (!handler)
                throw std::bad_alloc();
            handler();
        }
        void*       aligned = static_cast<std::byte*>(base) + sizeof(Header);
        std::size_t space   = allocated - sizeof(Header);
        (void) std::align(alignment, payload, aligned, space);
        auto* header = reinterpret_cast<Header*>(static_cast<std::byte*>(aligned) - sizeof(Header));
        std::construct_at(header, Header {base, size, allocated});
        const std::size_t live = liveBytes.fetch_add(size, std::memory_order_relaxed) + size;
        std::size_t       peak = peakBytes.load(std::memory_order_relaxed);
        while (peak < live && !peakBytes.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {}
        totalBytes.fetch_add(size, std::memory_order_relaxed);
        liveCount.fetch_add(1, std::memory_order_relaxed);
        totalCount.fetch_add(1, std::memory_order_relaxed);
        return aligned;
    }

    void Free(void* pointer) noexcept
    {
        if (!pointer)
            return;
        auto*        stored = reinterpret_cast<Header*>(static_cast<std::byte*>(pointer) - sizeof(Header));
        const Header header = *stored;
        std::destroy_at(stored);
        liveBytes.fetch_sub(header.requested, std::memory_order_relaxed);
        liveCount.fetch_sub(1, std::memory_order_relaxed);
        NGIN::Memory::SystemAllocator {}.Deallocate(header.base, header.allocated, alignof(std::max_align_t));
    }
}// namespace

namespace NGIN::Benchmarks::Allocations
{
    NGIN::Memory::AllocationStats Snapshot() noexcept
    {
        return {liveBytes.load(std::memory_order_relaxed), peakBytes.load(std::memory_order_relaxed),
                totalBytes.load(std::memory_order_relaxed), liveCount.load(std::memory_order_relaxed),
                totalCount.load(std::memory_order_relaxed)};
    }
    void ResetPeak() noexcept
    {
        peakBytes.store(liveBytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
}// namespace NGIN::Benchmarks::Allocations

void* operator new(std::size_t size)
{
    return Allocate(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}
void* operator new[](std::size_t size)
{
    return Allocate(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return Allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return Allocate(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* pointer) noexcept
{
    Free(pointer);
}
void operator delete[](void* pointer) noexcept
{
    Free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
    Free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
    Free(pointer);
}
void operator delete(void* pointer, std::align_val_t) noexcept
{
    Free(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept
{
    Free(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{
    Free(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept
{
    Free(pointer);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    } catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size);
    } catch (...)
    {
        return nullptr;
    }
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size, alignment);
    } catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size, alignment);
    } catch (...)
    {
        return nullptr;
    }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept
{
    Free(pointer);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
    Free(pointer);
}
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    Free(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    Free(pointer);
}

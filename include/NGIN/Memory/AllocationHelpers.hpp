/// @file AllocationHelpers.hpp
/// @brief Safe construction/destruction helpers built atop AllocatorConcept.
#pragma once

#include <new>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <stdexcept>
#include <cassert>

#include <NGIN/Memory/AllocatorConcept.hpp>

namespace NGIN::Memory
{
    namespace detail
    {
        struct ArrayHeader
        {
            std::size_t count;                                 // number of elements
            std::uint32_t magic;                               // sentinel for verification
            static constexpr std::uint32_t MAGIC = 0xA11A0C42u;// 'A','ll','oc' stylized
        };
    }// namespace detail

    template<AllocatorConcept A, class T, class... Args>
    [[nodiscard]] T* AllocateObject(A& alloc, Args&&... args)
    {
        void* mem = alloc.Allocate(sizeof(T), alignof(T));
        if (!mem)
            throw std::bad_alloc();
        try
        {
            return ::new (mem) T(std::forward<Args>(args)...);
        } catch (...)
        {
            alloc.Deallocate(mem, sizeof(T), alignof(T));
            throw;
        }
    }

    // Convenience overload: AllocateObject<T>(allocator, args...)
    template<class T, AllocatorConcept A, class... Args>
    [[nodiscard]] T* AllocateObject(A& alloc, Args&&... args)
    {
        return AllocateObject<A, T>(alloc, std::forward<Args>(args)...);
    }

    template<AllocatorConcept A, class T>
    void DeallocateObject(A& alloc, T* ptr) noexcept(std::is_nothrow_destructible_v<T>)
    {
        if (!ptr)
            return;
        ptr->~T();
        alloc.Deallocate(ptr, sizeof(T), alignof(T));
    }

    template<AllocatorConcept A, class T>
    [[nodiscard]] T* AllocateArrayUninitialized(A& alloc, std::size_t count)
    {
        if (count == 0)
            return nullptr;
        constexpr std::size_t AAlign = alignof(T);
        // Layout: raw: [ padding | header | (optional padding < AAlign) | elements ... ]
        // We align the ELEMENT region; header will live immediately before elements.
        const std::size_t bytesForElems = count * sizeof(T);
        const std::size_t rawSize       = bytesForElems + sizeof(detail::ArrayHeader) + (AAlign - 1);
        void* raw                       = alloc.Allocate(rawSize, AAlign);
        if (!raw)
            throw std::bad_alloc();
        std::byte* base            = static_cast<std::byte*>(raw);
        std::uintptr_t afterHeader = reinterpret_cast<std::uintptr_t>(base + sizeof(detail::ArrayHeader));
        std::uintptr_t alignedAddr = (afterHeader + (AAlign - 1)) & ~(static_cast<std::uintptr_t>(AAlign) - 1);
        T* arr                     = reinterpret_cast<T*>(alignedAddr);
        auto* header               = reinterpret_cast<detail::ArrayHeader*>(arr) - 1;// header lives immediately before array
        header->count              = count;
        header->magic              = detail::ArrayHeader::MAGIC;
        return arr;
    }

    // Allocate + value default construct each element
    template<AllocatorConcept A, class T>
    [[nodiscard]] T* AllocateArray(A& alloc, std::size_t count)
    {
        T* arr = AllocateArrayUninitialized<A, T>(alloc, count);
        if (!arr)
            return nullptr;
        std::size_t i = 0;
        try
        {
            for (; i < count; ++i)
                ::new (static_cast<void*>(&arr[i])) T();
        } catch (...)
        {
            for (std::size_t j = 0; j < i; ++j)
                arr[j].~T();
            // Recover header to free entire region.
            auto* header = reinterpret_cast<detail::ArrayHeader*>(arr) - 1;
            alloc.Deallocate(static_cast<void*>(header), 0, alignof(T));
            throw;
        }
        return arr;
    }

    // Convenience overload: AllocateArray<T>(allocator, count)
    template<class T, AllocatorConcept A>
    [[nodiscard]] T* AllocateArray(A& alloc, std::size_t count)
    {
        return AllocateArray<A, T>(alloc, count);
    }

    // Allocate array constructing each element with provided args (same args for each element)
    template<AllocatorConcept A, class T, class... Args>
    [[nodiscard]] T* AllocateArray(A& alloc, std::size_t count, const Args&... args)
    {
        T* arr = AllocateArrayUninitialized<A, T>(alloc, count);
        if (!arr)
            return nullptr;
        std::size_t i = 0;
        try
        {
            for (; i < count; ++i)
                ::new (static_cast<void*>(&arr[i])) T(args...);
        } catch (...)
        {
            for (std::size_t j = 0; j < i; ++j)
                arr[j].~T();
            auto* header = reinterpret_cast<detail::ArrayHeader*>(arr) - 1;
            alloc.Deallocate(static_cast<void*>(header), 0, alignof(T));
            throw;
        }
        return arr;
    }

    template<AllocatorConcept A, class T>
    void DeallocateArray(A& alloc, T* ptr) noexcept(std::is_nothrow_destructible_v<T>)
    {
        if (!ptr)
            return;
        auto* header = reinterpret_cast<detail::ArrayHeader*>(ptr) - 1;
        if (header->magic != detail::ArrayHeader::MAGIC)
            return;// corruption; debug assert below
#if defined(NGIN_DEBUG) || !defined(NDEBUG)
        if (!(header->count > 0 && header->count < (1ull << 40)))
        {
            // Likely memory corruption; abort in debug builds.
            assert(false && "AllocationHelpers::DeallocateArray header corruption detected");
            return;
        }
#endif
        std::size_t count = header->count;
        for (std::size_t i = count; i > 0; --i)
            ptr[i - 1].~T();
        alloc.Deallocate(static_cast<void*>(header), 0, alignof(T));
    }
}// namespace NGIN::Memory

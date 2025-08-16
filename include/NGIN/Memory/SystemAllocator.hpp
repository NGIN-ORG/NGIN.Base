/// @file SystemAllocator.hpp
/// @brief Stateless system allocation wrapper providing aligned allocations.
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>

#include <NGIN/Primitives.hpp>

namespace NGIN::Memory
{
    struct SystemAllocator
    {
        [[nodiscard]] static bool IsPowerOfTwo(UIntSize v) noexcept
        {
            return v && ((v & (v - 1)) == 0);
        }

        [[nodiscard]] void* Allocate(UIntSize size, UIntSize alignment) noexcept
        {
            if (size == 0)
                return nullptr;
            if (!IsPowerOfTwo(alignment))
                alignment = alignof(std::max_align_t);// fallback to safe alignment

#if defined(_WIN32) || defined(_WIN64)
            return _aligned_malloc(size, alignment);
#elif defined(__APPLE__) || defined(__unix__) || defined(__MACH__)
            void* p = nullptr;
            if (alignment < sizeof(void*))
                alignment = sizeof(void*);
            if (posix_memalign(&p, alignment, size) != 0)
                return nullptr;
            return p;
#elif defined(__cpp_aligned_new)
            if (size % alignment != 0)// std::aligned_alloc requires multiple of alignment
                size += alignment - (size % alignment);
            return std::aligned_alloc(alignment, size);
#else
            return std::malloc(size);// best-effort
#endif
        }

        void Deallocate(void* ptr, UIntSize, UIntSize) noexcept
        {
            if (!ptr)
                return;
#if defined(_WIN32) || defined(_WIN64)
            _aligned_free(ptr);
#else
            std::free(ptr);
#endif
        }

        [[nodiscard]] constexpr UIntSize MaxSize() const noexcept
        {
            return static_cast<UIntSize>(-1);
        }
        [[nodiscard]] constexpr UIntSize Remaining() const noexcept
        {
            return MaxSize();
        }
        [[nodiscard]] constexpr bool Owns(const void*) const noexcept
        {
            return true;
        }
    };
}// namespace NGIN::Memory

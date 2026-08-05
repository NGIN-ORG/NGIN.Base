/// @file SystemAllocator.hpp
/// @brief Stateless system allocation wrapper providing aligned allocations.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <type_traits>

#include <NGIN/Primitives.hpp>

namespace NGIN::Memory
{
    /// @brief Stateless allocator backed by the platform's aligned allocation API.
    struct SystemAllocator
    {
        /// @brief Returns whether a value is a nonzero power of two.
        [[nodiscard]] static bool IsPowerOfTwo(UIntSize v) noexcept
        {
            return v && ((v & (v - 1)) == 0);
        }

        /// @brief Allocates an aligned byte block.
        /// @param size Requested size in bytes; zero returns `nullptr`.
        /// @param alignment Requested alignment; invalid values fall back to `std::max_align_t`.
        /// @return Allocation base address, or `nullptr` on failure.
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

        /// @brief Releases a block previously returned by this allocator.
        /// @param ptr Allocation base address; `nullptr` is accepted.
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

        /// @brief Returns the largest representable allocation size.
        [[nodiscard]] constexpr UIntSize MaxSize() const noexcept
        {
            return static_cast<UIntSize>(-1);
        }
        /// @brief Returns the allocator's conceptual remaining capacity.
        [[nodiscard]] constexpr UIntSize Remaining() const noexcept
        {
            return MaxSize();
        }
        /// @brief Returns `true` because system allocations cannot be distinguished by instance.
        [[nodiscard]] constexpr bool Owns(const void*) const noexcept
        {
            return true;
        }
    };
}// namespace NGIN::Memory

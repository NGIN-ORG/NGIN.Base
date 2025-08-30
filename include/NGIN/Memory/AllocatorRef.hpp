/// @file FallbackAllocator.hpp
/// @brief Allocator that tries a primary allocator then falls back to secondary.
#pragma once

#include <utility>

#include <NGIN/Memory/AllocatorConcept.hpp>

namespace NGIN::Memory
{
    template<class A>
    class AllocatorRef
    {
    public:
        explicit AllocatorRef(A& a) : ptr_(&a) {}
        void* Allocate(std::size_t n, std::size_t a) noexcept { return ptr_->Allocate(n, a); }
        void  Deallocate(void* p, std::size_t n, std::size_t a) noexcept { ptr_->Deallocate(p, n, a); }

        // Optional capabilities, forward if present:
        std::size_t MaxSize() const noexcept
        {
            if constexpr (AllocatorReportsMaxSize<A>)
                return ptr_->MaxSize();
            else
                return SIZE_MAX;
        }
        std::size_t Remaining() const noexcept
        {
            if constexpr (AllocatorReportsRemainingBytes<A>)
                return ptr_->Remaining();
            else
                return MaxSize();
        }
        bool Owns(const void* p) const noexcept
        {
            if constexpr (AllocatorOwnsPointer<A>)
                return ptr_->Owns(p);
            else
                return true;
        }

    private:
        A* ptr_;
    };
}// namespace NGIN::Memory

/// @file AllocatorRef.hpp
/// @brief Non-owning reference wrapper that adapts an allocator instance to `AllocatorConcept`.
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
            return AllocatorTraits<A>::MaxSize(*ptr_);
        }
        std::size_t Remaining() const noexcept
        {
            return AllocatorTraits<A>::Remaining(*ptr_);
        }

        [[nodiscard]] Ownership OwnershipOf(const void* p) const noexcept
        {
            return AllocatorTraits<A>::OwnershipOf(*ptr_, p);
        }

        [[nodiscard]] bool Owns(const void* p) const noexcept
            requires AllocatorOwnsPointer<A>
        {
            return ptr_->Owns(p);
        }

    private:
        A* ptr_;
    };
}// namespace NGIN::Memory

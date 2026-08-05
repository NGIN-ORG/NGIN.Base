/// @file PolyAllocatorRef.hpp
/// @brief Type-erased allocator reference (non-owning) for rare dynamic dispatch cases.
#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

#include <NGIN/Memory/AllocatorConcept.hpp>

namespace NGIN::Memory
{
    /// @brief Non-owning type-erased reference to an allocator.
    ///
    /// The referenced allocator must outlive this object and every allocation made through it.
    class PolyAllocatorRef
    {
    public:
        /// @brief Constructs an empty allocator reference that rejects allocations.
        PolyAllocatorRef() noexcept
            : m_object(nullptr),
              m_vt({
                      // Allocate
                      [](void*, std::size_t, std::size_t) noexcept -> void* { return nullptr; },
                      // Deallocate
                      [](void*, void*, std::size_t, std::size_t) noexcept {},
                      // AllocateEx
                      [](void*, std::size_t, std::size_t) noexcept -> MemoryBlock { return {}; },
                      // MaxSize
                      [](const void*) noexcept -> std::size_t { return 0; },
                      // Remaining
                      [](const void*) noexcept -> std::size_t { return 0; },
                      // OwnershipOf
                      [](const void*, const void*) noexcept -> Ownership { return Ownership::Unknown; },
              })
        {
        }

        PolyAllocatorRef(const PolyAllocatorRef&) noexcept            = default;
        PolyAllocatorRef& operator=(const PolyAllocatorRef&) noexcept = default;
        PolyAllocatorRef(PolyAllocatorRef&&) noexcept                 = default;
        PolyAllocatorRef& operator=(PolyAllocatorRef&&) noexcept      = default;

        /// @brief Binds this reference to a concrete allocator without taking ownership.
        /// @tparam A Concrete allocator type.
        /// @param allocator Allocator that must outlive this reference.
        template<AllocatorConcept A>
            requires(!std::same_as<std::remove_cvref_t<A>, PolyAllocatorRef>)
        explicit PolyAllocatorRef(A& allocator) noexcept
            : m_object(&allocator),
              m_vt({
                      // Allocate
                      [](void* o, std::size_t n, std::size_t al) noexcept -> void* {
                          return static_cast<A*>(o)->Allocate(n, al);
                      },
                      // Deallocate
                      [](void* o, void* p, std::size_t n, std::size_t al) noexcept {
                          static_cast<A*>(o)->Deallocate(p, n, al);
                      },
                      // AllocateEx
                      [](void* o, std::size_t n, std::size_t al) noexcept -> MemoryBlock {
                          return AllocatorTraits<A>::AllocateEx(*static_cast<A*>(o), n, al);
                      },
                      // MaxSize
                      [](const void* o) noexcept -> std::size_t {
                          return AllocatorTraits<A>::MaxSize(*static_cast<const A*>(o));
                      },
                      // Remaining
                      [](const void* o) noexcept -> std::size_t {
                          return AllocatorTraits<A>::Remaining(*static_cast<const A*>(o));
                      },
                      // OwnershipOf
                      [](const void* o, const void* p) noexcept -> Ownership {
                          return AllocatorTraits<A>::OwnershipOf(*static_cast<const A*>(o), p);
                      },
              })
        {
        }

        /// @brief Allocates a block through the referenced allocator.
        [[nodiscard]] void* Allocate(std::size_t n, std::size_t alignmentInBytes) noexcept
        {
            return m_vt.allocate(m_object, n, alignmentInBytes);
        }

        /// @brief Releases a block through the referenced allocator.
        void Deallocate(void* p, std::size_t n, std::size_t alignmentInBytes) noexcept
        {
            m_vt.deallocate(m_object, p, n, alignmentInBytes);
        }

        /// @brief Allocates a block and returns allocator-defined ownership metadata.
        [[nodiscard]] MemoryBlock AllocateEx(std::size_t n, std::size_t alignmentInBytes) noexcept
        {
            return m_vt.allocateEx(m_object, n, alignmentInBytes);
        }

        /// @brief Returns the maximum allocation size reported by the referenced allocator.
        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            return m_vt.maxSize(m_object);
        }

        /// @brief Returns the remaining capacity reported by the referenced allocator.
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            return m_vt.remaining(m_object);
        }

        /// @brief Classifies whether a pointer belongs to the referenced allocator.
        [[nodiscard]] Ownership OwnershipOf(const void* p) const noexcept
        {
            return m_vt.ownershipOf(m_object, p);
        }

        /// @brief Returns whether this object references a concrete allocator.
        [[nodiscard]] bool HasValue() const noexcept { return m_object != nullptr; }
        /// @brief Returns whether this object references a concrete allocator.
        explicit operator bool() const noexcept { return HasValue(); }

    private:
        struct VTable
        {
            void* (*allocate)(void*, std::size_t, std::size_t) noexcept;
            void (*deallocate)(void*, void*, std::size_t, std::size_t) noexcept;
            MemoryBlock (*allocateEx)(void*, std::size_t, std::size_t) noexcept;
            std::size_t (*maxSize)(const void*) noexcept;
            std::size_t (*remaining)(const void*) noexcept;
            Ownership (*ownershipOf)(const void*, const void*) noexcept;
        };

        void*  m_object;
        VTable m_vt;
    };

    /// @brief Backward-compatible short name for PolyAllocatorRef.
    using PolyAllocator = PolyAllocatorRef;

    static_assert(AllocatorConcept<PolyAllocatorRef>);

}// namespace NGIN::Memory

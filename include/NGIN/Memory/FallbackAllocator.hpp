/// @file FallbackAllocator.hpp
/// @brief Allocator that tries a primary allocator then falls back to secondary.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/detail/CheckedArithmetic.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace NGIN::Memory
{
    /// @brief Routes allocations to a primary allocator and falls back to a secondary allocator.
    /// @details Both allocators must provide precise ownership queries so deallocation can be routed safely.
    template<AllocatorConcept Primary, AllocatorConcept Secondary>
    class FallbackAllocator
    {
    public:
        static_assert(AllocatorReportsPreciseOwnership<Primary> && AllocatorReportsPreciseOwnership<Secondary>,
                      "FallbackAllocator requires definitive OwnershipOf() results from both allocators. "
                      "Use TaggedFallbackAllocator when either allocator can return Ownership::Unknown.");

        /// @brief Both underlying allocators provide definitive ownership queries.
        static constexpr bool HasPreciseOwnership = true;

        /// @brief Constructs both underlying allocators with their defaults.
        FallbackAllocator() = default;

        /// @brief Constructs the allocator from primary and secondary allocator instances.
        FallbackAllocator(Primary p, Secondary s) : m_primary(std::move(p)), m_secondary(std::move(s)) {}

        /// @brief Allocates from the primary allocator or, on failure, from the secondary allocator.
        /// @return Allocation base address, or `nullptr` when both allocators fail.
        [[nodiscard]] void* Allocate(std::size_t n, std::size_t a) noexcept
        {
            if (void* p = m_primary.Allocate(n, a))
                return p;
            return m_secondary.Allocate(n, a);
        }
        /// @brief Releases a block through the underlying allocator that owns it.
        void Deallocate(void* ptr, std::size_t n, std::size_t a) noexcept
        {
            if (!ptr)
                return;

            if (m_primary.OwnershipOf(ptr) == Ownership::Owns)
            {
                m_primary.Deallocate(ptr, n, a);
                return;
            }

            if (m_secondary.OwnershipOf(ptr) == Ownership::Owns)
            {
                m_secondary.Deallocate(ptr, n, a);
                return;
            }

            assert(false && "FallbackAllocator received a pointer owned by neither allocator");
        }
        /// @brief Returns the saturating sum of both allocators' maximum allocation sizes.
        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            const std::size_t a = AllocatorTraits<Primary>::MaxSize(m_primary);
            const std::size_t b = AllocatorTraits<Secondary>::MaxSize(m_secondary);
            if (a > (std::numeric_limits<std::size_t>::max() - b))
                return std::numeric_limits<std::size_t>::max();
            return a + b;
        }
        /// @brief Returns the saturating sum of both allocators' remaining capacities.
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            const std::size_t a = AllocatorTraits<Primary>::Remaining(m_primary);
            const std::size_t b = AllocatorTraits<Secondary>::Remaining(m_secondary);
            if (a > (std::numeric_limits<std::size_t>::max() - b))
                return std::numeric_limits<std::size_t>::max();
            return a + b;
        }
        /// @brief Returns a definitive ownership result from the underlying allocators.
        [[nodiscard]] Ownership OwnershipOf(const void* p) const noexcept
        {
            if (m_primary.OwnershipOf(p) == Ownership::Owns || m_secondary.OwnershipOf(p) == Ownership::Owns)
                return Ownership::Owns;
            return Ownership::DoesNotOwn;
        }

    private:
        NGIN_NO_UNIQUE_ADDRESS Primary   m_primary {};
        NGIN_NO_UNIQUE_ADDRESS Secondary m_secondary {};
    };

    namespace detail
    {
        struct TaggedHeader
        {
            void*         rawBase {nullptr};
            std::size_t   rawSizeInBytes {0};
            std::size_t   rawAlignmentInBytes {0};
            std::uint32_t magic {0};
            std::uint8_t  tag {0};
            std::uint8_t  padding[3] {};

            static constexpr std::uint32_t MAGIC = 0x7A67F00Du;
        };

        static_assert((sizeof(TaggedHeader) % alignof(TaggedHeader)) == 0);
    }// namespace detail

    /// @brief Fallback allocator that records the allocation route in an adjacent header.
    /// @details Tagging supports safe deallocation even when underlying allocators cannot report ownership.
    template<AllocatorConcept Primary, AllocatorConcept Secondary>
    class TaggedFallbackAllocator
    {
    public:
        /// @brief Constructs both underlying allocators with their defaults.
        TaggedFallbackAllocator() = default;

        /// @brief Constructs the allocator from primary and secondary allocator instances.
        TaggedFallbackAllocator(Primary p, Secondary s) : m_primary(std::move(p)), m_secondary(std::move(s)) {}

        /// @brief Allocates a tagged block from the primary or secondary allocator.
        /// @return Aligned user address, or `nullptr` when both allocators fail.
        [[nodiscard]] void* Allocate(std::size_t n, std::size_t alignmentInBytes) noexcept
        {
            if (n == 0)
                return nullptr;
            if (void* p = AllocateTagged_(m_primary, n, alignmentInBytes, 1))
                return p;
            return AllocateTagged_(m_secondary, n, alignmentInBytes, 2);
        }

        /// @brief Allocates a tagged block and reports its effective alignment and route tag.
        [[nodiscard]] MemoryBlock AllocateEx(std::size_t n, std::size_t alignmentInBytes) noexcept
        {
            void* p = Allocate(n, alignmentInBytes);
            if (!p)
                return {};
            const std::uint8_t tag                 = HeaderFromUserPointer_(p)->tag;
            std::size_t        normalizedAlignment = 0;
            if (!detail::TryNormalizeAlignment(alignmentInBytes, alignof(detail::TaggedHeader), normalizedAlignment))
                return {};
            return MemoryBlock {p, n, normalizedAlignment, tag};
        }

        /// @brief Releases a tagged block through the allocator recorded in its header.
        /// @pre `ptr` is null or was returned by this allocator and has not already been released.
        /// @warning The route header cannot be inspected safely for an arbitrary foreign pointer.
        void Deallocate(void* ptr, std::size_t, std::size_t) noexcept
        {
            if (!ptr)
                return;
            detail::TaggedHeader* header = HeaderFromUserPointer_(ptr);
            assert(header->magic == detail::TaggedHeader::MAGIC &&
                   "TaggedFallbackAllocator requires a pointer returned by this allocator");
            if (header->magic != detail::TaggedHeader::MAGIC)
                return;

            if (header->tag == 1)
                m_primary.Deallocate(header->rawBase, header->rawSizeInBytes, header->rawAlignmentInBytes);
            else if (header->tag == 2)
                m_secondary.Deallocate(header->rawBase, header->rawSizeInBytes, header->rawAlignmentInBytes);
            else
                assert(false && "TaggedFallbackAllocator allocation header has an invalid route tag");
        }

        /// @brief Returns the saturating sum of both allocators' maximum allocation sizes.
        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            const std::size_t a = AllocatorTraits<Primary>::MaxSize(m_primary);
            const std::size_t b = AllocatorTraits<Secondary>::MaxSize(m_secondary);
            if (a > (std::numeric_limits<std::size_t>::max() - b))
                return std::numeric_limits<std::size_t>::max();
            return a + b;
        }

        /// @brief Returns the saturating sum of both allocators' remaining capacities.
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            const std::size_t a = AllocatorTraits<Primary>::Remaining(m_primary);
            const std::size_t b = AllocatorTraits<Secondary>::Remaining(m_secondary);
            if (a > (std::numeric_limits<std::size_t>::max() - b))
                return std::numeric_limits<std::size_t>::max();
            return a + b;
        }

        /// @brief Returns the primary allocator.
        Primary& PrimaryAllocator() noexcept { return m_primary; }

        /// @brief Returns the primary allocator.
        const Primary& PrimaryAllocator() const noexcept { return m_primary; }

        /// @brief Returns the secondary allocator.
        Secondary& SecondaryAllocator() noexcept { return m_secondary; }

        /// @brief Returns the secondary allocator.
        const Secondary& SecondaryAllocator() const noexcept { return m_secondary; }

    private:
        [[nodiscard]] static detail::TaggedHeader* HeaderFromUserPointer_(void* userPtr) noexcept
        {
            return reinterpret_cast<detail::TaggedHeader*>(userPtr) - 1;
        }

        template<class Alloc>
        [[nodiscard]] void* AllocateTagged_(Alloc& alloc, std::size_t n, std::size_t alignmentInBytes, std::uint8_t tag) noexcept
        {
            std::size_t normalizedAlignment = 0;
            if (!detail::TryNormalizeAlignment(alignmentInBytes, alignof(detail::TaggedHeader), normalizedAlignment))
                return nullptr;

            std::size_t rawSizeInBytes = 0;
            if (!detail::CheckedAdd(n, sizeof(detail::TaggedHeader), rawSizeInBytes) ||
                !detail::CheckedAdd(rawSizeInBytes, normalizedAlignment - 1, rawSizeInBytes))
                return nullptr;

            void* raw = alloc.Allocate(rawSizeInBytes, normalizedAlignment);
            if (!raw)
                return nullptr;

            std::byte*  base    = static_cast<std::byte*>(raw);
            void*       start   = base + sizeof(detail::TaggedHeader);
            std::size_t space   = rawSizeInBytes - sizeof(detail::TaggedHeader);
            void*       aligned = start;

            if (std::align(normalizedAlignment, n, aligned, space) == nullptr)
            {
                alloc.Deallocate(raw, rawSizeInBytes, normalizedAlignment);
                return nullptr;
            }

            detail::TaggedHeader* header = reinterpret_cast<detail::TaggedHeader*>(aligned) - 1;
            header->rawBase              = raw;
            header->rawSizeInBytes       = rawSizeInBytes;
            header->rawAlignmentInBytes  = normalizedAlignment;
            header->magic                = detail::TaggedHeader::MAGIC;
            header->tag                  = tag;

            return aligned;
        }

        NGIN_NO_UNIQUE_ADDRESS Primary   m_primary {};
        NGIN_NO_UNIQUE_ADDRESS Secondary m_secondary {};
    };
}// namespace NGIN::Memory

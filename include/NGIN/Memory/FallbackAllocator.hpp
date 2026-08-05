/// @file FallbackAllocator.hpp
/// @brief Allocator that tries a primary allocator then falls back to secondary.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include <NGIN/Memory/AllocatorConcept.hpp>

namespace NGIN::Memory
{
    /// @brief Routes allocations to a primary allocator and falls back to a secondary allocator.
    /// @details Both allocators must provide precise ownership queries so deallocation can be routed safely.
    template<AllocatorConcept Primary, AllocatorConcept Secondary>
    class FallbackAllocator
    {
    public:
        static_assert(AllocatorOwnsPointer<Primary> && AllocatorOwnsPointer<Secondary>,
                      "FallbackAllocator requires Owns() on both allocators. Use TaggedFallbackAllocator instead.");

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
            if (m_primary.Owns(ptr))
                m_primary.Deallocate(ptr, n, a);
            else
                m_secondary.Deallocate(ptr, n, a);
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
        /// @brief Returns whether either underlying allocator owns a pointer.
        [[nodiscard]] bool Owns(const void* p) const noexcept
        {
            return m_primary.Owns(p) || m_secondary.Owns(p);
        }

    private:
        [[no_unique_address]] Primary   m_primary {};
        [[no_unique_address]] Secondary m_secondary {};
    };

    namespace detail
    {
        constexpr bool IsPowerOfTwo(std::size_t value) noexcept
        {
            return value && ((value & (value - 1)) == 0);
        }

        constexpr std::size_t NormalizeAlignment(std::size_t alignmentInBytes) noexcept
        {
            if (alignmentInBytes == 0)
                alignmentInBytes = 1;
            if (!IsPowerOfTwo(alignmentInBytes))
            {
                std::size_t a = alignmentInBytes - 1;
                a |= a >> 1;
                a |= a >> 2;
                a |= a >> 4;
                a |= a >> 8;
                a |= a >> 16;
#if INTPTR_MAX == INT64_MAX
                a |= a >> 32;
#endif
                alignmentInBytes = a + 1;
            }
            return alignmentInBytes;
        }

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
            const std::uint8_t tag = HeaderFromUserPointer_(p)->tag;
            return MemoryBlock {p, n, detail::NormalizeAlignment(alignmentInBytes), tag};
        }

        /// @brief Releases a tagged block through the allocator recorded in its header.
        /// @warning Passing a pointer not produced by this allocator is ignored.
        void Deallocate(void* ptr, std::size_t, std::size_t) noexcept
        {
            if (!ptr)
                return;
            detail::TaggedHeader* header = HeaderFromUserPointer_(ptr);
            if (header->magic != detail::TaggedHeader::MAGIC)
                return;

            if (header->tag == 1)
                m_primary.Deallocate(header->rawBase, header->rawSizeInBytes, header->rawAlignmentInBytes);
            else
                m_secondary.Deallocate(header->rawBase, header->rawSizeInBytes, header->rawAlignmentInBytes);
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

        /// @brief Queries whether either underlying allocator positively owns a pointer.
        /// @return `Ownership::Owns` on a positive result; otherwise `Ownership::Unknown`.
        [[nodiscard]] Ownership OwnershipOf(const void* p) const noexcept
        {
            return AllocatorTraits<Primary>::OwnershipOf(m_primary, p) == Ownership::Owns       ? Ownership::Owns
                   : AllocatorTraits<Secondary>::OwnershipOf(m_secondary, p) == Ownership::Owns ? Ownership::Owns
                                                                                                : Ownership::Unknown;
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
            const std::size_t normalizedAlignment =
                    (std::max) (detail::NormalizeAlignment(alignmentInBytes), alignof(detail::TaggedHeader));

            if (n > std::numeric_limits<std::size_t>::max() - sizeof(detail::TaggedHeader) - (normalizedAlignment - 1))
                return nullptr;
            const std::size_t rawSizeInBytes = n + sizeof(detail::TaggedHeader) + (normalizedAlignment - 1);

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

        [[no_unique_address]] Primary   m_primary {};
        [[no_unique_address]] Secondary m_secondary {};
    };
}// namespace NGIN::Memory

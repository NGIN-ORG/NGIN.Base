/// @file FallbackAllocator.hpp
/// @brief Allocator that tries a primary allocator then falls back to secondary.
#pragma once

#include <utility>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <memory>

#include <NGIN/Memory/AllocatorConcept.hpp>

namespace NGIN::Memory
{
    template<AllocatorConcept Primary, AllocatorConcept Secondary>
    class FallbackAllocator
    {
    public:
        static_assert(AllocatorOwnsPointer<Primary> && AllocatorOwnsPointer<Secondary>,
                      "FallbackAllocator requires Owns() on both allocators. Use TaggedFallbackAllocator instead.");

        FallbackAllocator() = default;
        FallbackAllocator(Primary p, Secondary s) : m_primary(std::move(p)), m_secondary(std::move(s)) {}

        [[nodiscard]] void* Allocate(std::size_t n, std::size_t a) noexcept
        {
            if (void* p = m_primary.Allocate(n, a))
                return p;
            return m_secondary.Allocate(n, a);
        }
        void Deallocate(void* ptr, std::size_t n, std::size_t a) noexcept
        {
            if (m_primary.Owns(ptr))
                m_primary.Deallocate(ptr, n, a);
            else
                m_secondary.Deallocate(ptr, n, a);
        }
        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            const auto a = AllocatorTraits<Primary>::MaxSize(m_primary);
            const auto b = AllocatorTraits<Secondary>::MaxSize(m_secondary);
            if (a > (std::numeric_limits<std::size_t>::max() - b))
                return std::numeric_limits<std::size_t>::max();
            return a + b;
        }
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            const auto a = AllocatorTraits<Primary>::Remaining(m_primary);
            const auto b = AllocatorTraits<Secondary>::Remaining(m_secondary);
            if (a > (std::numeric_limits<std::size_t>::max() - b))
                return std::numeric_limits<std::size_t>::max();
            return a + b;
        }
        [[nodiscard]] bool Owns(const void* p) const noexcept
        {
            return m_primary.Owns(p) || m_secondary.Owns(p);
        }

    private:
        [[no_unique_address]] Primary m_primary {};
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

    template<AllocatorConcept Primary, AllocatorConcept Secondary>
    class TaggedFallbackAllocator
    {
    public:
        TaggedFallbackAllocator() = default;
        TaggedFallbackAllocator(Primary p, Secondary s) : m_primary(std::move(p)), m_secondary(std::move(s)) {}

        [[nodiscard]] void* Allocate(std::size_t n, std::size_t alignmentInBytes) noexcept
        {
            if (n == 0)
                return nullptr;
            if (void* p = AllocateTagged_(m_primary, n, alignmentInBytes, 1))
                return p;
            return AllocateTagged_(m_secondary, n, alignmentInBytes, 2);
        }

        [[nodiscard]] MemoryBlock AllocateEx(std::size_t n, std::size_t alignmentInBytes) noexcept
        {
            void* p = Allocate(n, alignmentInBytes);
            if (!p)
                return {};
            const auto tag = HeaderFromUserPointer_(p)->tag;
            return MemoryBlock {p, n, detail::NormalizeAlignment(alignmentInBytes), tag};
        }

        void Deallocate(void* ptr, std::size_t, std::size_t) noexcept
        {
            if (!ptr)
                return;
            auto* header = HeaderFromUserPointer_(ptr);
            if (header->magic != detail::TaggedHeader::MAGIC)
                return;

            if (header->tag == 1)
                m_primary.Deallocate(header->rawBase, header->rawSizeInBytes, header->rawAlignmentInBytes);
            else
                m_secondary.Deallocate(header->rawBase, header->rawSizeInBytes, header->rawAlignmentInBytes);
        }

        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            const auto a = AllocatorTraits<Primary>::MaxSize(m_primary);
            const auto b = AllocatorTraits<Secondary>::MaxSize(m_secondary);
            if (a > (std::numeric_limits<std::size_t>::max() - b))
                return std::numeric_limits<std::size_t>::max();
            return a + b;
        }

        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            const auto a = AllocatorTraits<Primary>::Remaining(m_primary);
            const auto b = AllocatorTraits<Secondary>::Remaining(m_secondary);
            if (a > (std::numeric_limits<std::size_t>::max() - b))
                return std::numeric_limits<std::size_t>::max();
            return a + b;
        }

        [[nodiscard]] Ownership OwnershipOf(const void* p) const noexcept
        {
            return AllocatorTraits<Primary>::OwnershipOf(m_primary, p) == Ownership::Owns ? Ownership::Owns
                   : AllocatorTraits<Secondary>::OwnershipOf(m_secondary, p) == Ownership::Owns ? Ownership::Owns
                                                                                                 : Ownership::Unknown;
        }

        Primary&       PrimaryAllocator() noexcept { return m_primary; }
        const Primary& PrimaryAllocator() const noexcept { return m_primary; }
        Secondary&       SecondaryAllocator() noexcept { return m_secondary; }
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
                    (std::max)(detail::NormalizeAlignment(alignmentInBytes), alignof(detail::TaggedHeader));

            if (n > std::numeric_limits<std::size_t>::max() - sizeof(detail::TaggedHeader) - (normalizedAlignment - 1))
                return nullptr;
            const std::size_t rawSizeInBytes = n + sizeof(detail::TaggedHeader) + (normalizedAlignment - 1);

            void* raw = alloc.Allocate(rawSizeInBytes, normalizedAlignment);
            if (!raw)
                return nullptr;

            std::byte* base  = static_cast<std::byte*>(raw);
            void*      start = base + sizeof(detail::TaggedHeader);
            std::size_t space = rawSizeInBytes - sizeof(detail::TaggedHeader);
            void*       aligned = start;

            if (std::align(normalizedAlignment, n, aligned, space) == nullptr)
            {
                alloc.Deallocate(raw, rawSizeInBytes, normalizedAlignment);
                return nullptr;
            }

            auto* header               = reinterpret_cast<detail::TaggedHeader*>(aligned) - 1;
            header->rawBase            = raw;
            header->rawSizeInBytes     = rawSizeInBytes;
            header->rawAlignmentInBytes = normalizedAlignment;
            header->magic              = detail::TaggedHeader::MAGIC;
            header->tag                = tag;

            return aligned;
        }

        [[no_unique_address]] Primary   m_primary {};
        [[no_unique_address]] Secondary m_secondary {};
    };
}// namespace NGIN::Memory

/// @file DebugAllocator.hpp
/// @brief Canary, poisoning, and invalid-free diagnostics for an inner allocator.
#pragma once

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace NGIN::Memory
{
    struct DebugAllocatorStats
    {
        std::size_t liveAllocations {0};
        std::size_t invalidDeallocations {0};
        std::size_t corruptedAllocations {0};
    };

    template<AllocatorConcept Inner = SystemAllocator>
    class DebugAllocator
    {
        struct Header
        {
            void*         raw {nullptr};
            std::size_t   rawSize {0};
            std::size_t   rawAlignment {0};
            std::size_t   requestedSize {0};
            std::uint64_t canary {0};
        };

        struct Record
        {
            void*       pointer {nullptr};
            Header*     header {nullptr};
            void*       raw {nullptr};
            std::size_t rawSize {0};
            std::size_t rawAlignment {0};
            std::size_t requestedSize {0};
        };

        static constexpr std::uint64_t Canary = 0xD38B'5A71'C4E2'9F06ULL;

    public:
        explicit DebugAllocator(Inner inner = {})
            : m_inner(std::move(inner))
        {
        }

        DebugAllocator(const DebugAllocator&)                        = delete;
        auto operator=(const DebugAllocator&) -> DebugAllocator&     = delete;
        DebugAllocator(DebugAllocator&&) noexcept                    = default;
        auto operator=(DebugAllocator&&) noexcept -> DebugAllocator& = default;

        [[nodiscard]] void* Allocate(const std::size_t bytes, const std::size_t alignment)
        {
            if (bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
                return nullptr;
            const std::size_t effectiveAlignment = (std::max) (alignment, alignof(Header));
            if (bytes > (std::numeric_limits<std::size_t>::max)() - sizeof(Header) - sizeof(Canary) - effectiveAlignment)
                return nullptr;
            const std::size_t rawSize = bytes + sizeof(Header) + sizeof(Canary) + effectiveAlignment - 1;
            void*             raw     = m_inner.Allocate(rawSize, effectiveAlignment);
            if (!raw)
                return nullptr;

            auto address  = reinterpret_cast<std::uintptr_t>(raw) + sizeof(Header);
            address       = (address + effectiveAlignment - 1) & ~(effectiveAlignment - 1);
            void* pointer = reinterpret_cast<void*>(address);
            auto* header  = reinterpret_cast<Header*>(address - sizeof(Header));
            *header       = Header {
                    .raw           = raw,
                    .rawSize       = rawSize,
                    .rawAlignment  = effectiveAlignment,
                    .requestedSize = bytes,
                    .canary        = Canary,
            };
            std::memset(pointer, 0xCD, bytes);
            std::memcpy(static_cast<std::byte*>(pointer) + bytes, &Canary, sizeof(Canary));

            try
            {
                m_live.push_back({pointer, header, raw, rawSize, effectiveAlignment, bytes});
            } catch (...)
            {
                m_inner.Deallocate(raw, rawSize, effectiveAlignment);
                throw;
            }
            return pointer;
        }

        void Deallocate(void* pointer, std::size_t, std::size_t) noexcept
        {
            if (!pointer)
                return;
            const auto found = std::find_if(m_live.begin(), m_live.end(), [pointer](const Record& record) {
                return record.pointer == pointer;
            });
            if (found == m_live.end())
            {
                ++m_stats.invalidDeallocations;
                return;
            }

            Header*       header = found->header;
            std::uint64_t tail {0};
            std::memcpy(&tail, static_cast<std::byte*>(pointer) + found->requestedSize, sizeof(tail));
            if (header->canary != Canary || header->raw != found->raw || header->rawSize != found->rawSize ||
                header->rawAlignment != found->rawAlignment || header->requestedSize != found->requestedSize || tail != Canary)
                ++m_stats.corruptedAllocations;

            std::memset(pointer, 0xDD, found->requestedSize);
            void*       raw          = found->raw;
            std::size_t rawSize      = found->rawSize;
            std::size_t rawAlignment = found->rawAlignment;
            m_live.erase(found);
            m_inner.Deallocate(raw, rawSize, rawAlignment);
        }

        [[nodiscard]] MemoryBlock AllocateEx(const std::size_t bytes, const std::size_t alignment)
        {
            void* pointer = Allocate(bytes, alignment);
            return {pointer, pointer ? bytes : 0, pointer ? alignment : 0};
        }

        [[nodiscard]] bool Owns(const void* pointer) const noexcept
        {
            return std::find_if(m_live.begin(), m_live.end(), [pointer](const Record& record) {
                       return record.pointer == pointer;
                   }) != m_live.end();
        }

        [[nodiscard]] DebugAllocatorStats GetStats() const noexcept
        {
            auto stats            = m_stats;
            stats.liveAllocations = m_live.size();
            return stats;
        }

        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            const auto     inner    = AllocatorTraits<Inner>::MaxSize(m_inner);
            constexpr auto overhead = sizeof(Header) + sizeof(Canary) + alignof(Header) - 1;
            return inner > overhead ? inner - overhead : 0;
        }

        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            const auto     inner    = AllocatorTraits<Inner>::Remaining(m_inner);
            constexpr auto overhead = sizeof(Header) + sizeof(Canary) + alignof(Header) - 1;
            return inner > overhead ? inner - overhead : 0;
        }

    private:
        [[no_unique_address]] Inner m_inner {};
        std::vector<Record>         m_live {};
        DebugAllocatorStats         m_stats {};
    };
}// namespace NGIN::Memory

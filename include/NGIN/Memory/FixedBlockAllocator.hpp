/// @file FixedBlockAllocator.hpp
/// @brief Owning, fixed-capacity allocator for one size and alignment class.
#pragma once

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace NGIN::Memory
{
    template<std::size_t      BlockSize,
             std::size_t      BlockCount,
             std::size_t      Alignment = alignof(std::max_align_t),
             AllocatorConcept Upstream  = SystemAllocator>
    class FixedBlockAllocator
    {
        static_assert(BlockSize > 0);
        static_assert(BlockCount > 0);
        static_assert(Alignment > 0 && (Alignment & (Alignment - 1)) == 0);

        struct FreeNode
        {
            FreeNode* next {nullptr};
        };

        static_assert(Alignment >= alignof(FreeNode));

        static constexpr std::size_t PayloadSize = (std::max) (BlockSize, sizeof(FreeNode));
        static constexpr std::size_t Stride      = (PayloadSize + Alignment - 1) & ~(Alignment - 1);
        static constexpr std::size_t SlabSize    = Stride * BlockCount;

    public:
        explicit FixedBlockAllocator(Upstream upstream = {})
            : m_upstream(std::move(upstream))
        {
            m_base = static_cast<std::byte*>(m_upstream.Allocate(SlabSize, Alignment));
            if (m_base)
                ResetFreeList();
        }

        FixedBlockAllocator(const FixedBlockAllocator&)                    = delete;
        auto operator=(const FixedBlockAllocator&) -> FixedBlockAllocator& = delete;

        FixedBlockAllocator(FixedBlockAllocator&& other) noexcept
            requires std::is_nothrow_move_constructible_v<Upstream>
            : m_upstream(std::move(other.m_upstream)), m_base(std::exchange(other.m_base, nullptr)), m_free(std::exchange(other.m_free, nullptr)), m_available(std::exchange(other.m_available, 0)), m_invalidDeallocations(std::exchange(other.m_invalidDeallocations, 0)), m_allocated(std::exchange(other.m_allocated, std::array<bool, BlockCount> {}))
        {
        }

        auto operator=(FixedBlockAllocator&& other) noexcept -> FixedBlockAllocator&
            requires(std::is_nothrow_move_assignable_v<Upstream>)
        {
            if (this != &other)
            {
                Release();
                m_upstream             = std::move(other.m_upstream);
                m_base                 = std::exchange(other.m_base, nullptr);
                m_free                 = std::exchange(other.m_free, nullptr);
                m_available            = std::exchange(other.m_available, 0);
                m_invalidDeallocations = std::exchange(other.m_invalidDeallocations, 0);
                m_allocated            = std::exchange(other.m_allocated, std::array<bool, BlockCount> {});
            }
            return *this;
        }

        ~FixedBlockAllocator() { Release(); }

        [[nodiscard]] void* Allocate(const std::size_t bytes, const std::size_t alignment) noexcept
        {
            if (bytes == 0 || bytes > BlockSize || alignment == 0 || alignment > Alignment ||
                (alignment & (alignment - 1)) != 0 || !m_free)
                return nullptr;

            FreeNode* node     = m_free;
            m_free             = node->next;
            const auto index   = static_cast<std::size_t>(reinterpret_cast<std::byte*>(node) - m_base) / Stride;
            m_allocated[index] = true;
            --m_available;
            return node;
        }

        void Deallocate(void* pointer, std::size_t, std::size_t) noexcept
        {
            if (!pointer)
                return;
            if (!IsBlockStart(pointer))
            {
                ++m_invalidDeallocations;
                return;
            }

            const auto index = static_cast<std::size_t>(static_cast<std::byte*>(pointer) - m_base) / Stride;
            if (!m_allocated[index])
            {
                ++m_invalidDeallocations;
                return;
            }

            auto* node = static_cast<FreeNode*>(pointer);
            node->next = m_free;
            m_free     = node;
            ++m_available;
            m_allocated[index] = false;
        }

        [[nodiscard]] MemoryBlock AllocateEx(const std::size_t bytes, const std::size_t alignment) noexcept
        {
            void* pointer = Allocate(bytes, alignment);
            return {pointer, pointer ? BlockSize : 0, pointer ? Alignment : 0};
        }

        [[nodiscard]] bool Owns(const void* pointer) const noexcept
        {
            if (!m_base || !pointer)
                return false;
            const auto* bytes = static_cast<const std::byte*>(pointer);
            return bytes >= m_base && bytes < m_base + SlabSize;
        }

        [[nodiscard]] bool IsBlockStart(const void* pointer) const noexcept
        {
            if (!Owns(pointer))
                return false;
            return static_cast<std::size_t>(static_cast<const std::byte*>(pointer) - m_base) % Stride == 0;
        }

        [[nodiscard]] static constexpr std::size_t MaxSize() noexcept { return BlockSize; }
        [[nodiscard]] std::size_t                  Remaining() const noexcept { return m_available * BlockSize; }
        [[nodiscard]] std::size_t                  Capacity() const noexcept { return BlockCount; }
        [[nodiscard]] std::size_t                  AvailableBlocks() const noexcept { return m_available; }
        [[nodiscard]] std::size_t                  InvalidDeallocations() const noexcept { return m_invalidDeallocations; }

    private:
        void ResetFreeList() noexcept
        {
            m_free = nullptr;
            for (std::size_t index = BlockCount; index > 0; --index)
            {
                auto* node = reinterpret_cast<FreeNode*>(m_base + (index - 1) * Stride);
                node->next = m_free;
                m_free     = node;
            }
            m_available = BlockCount;
            m_allocated.fill(false);
        }

        void Release() noexcept
        {
            if (m_base)
                m_upstream.Deallocate(m_base, SlabSize, Alignment);
            m_base      = nullptr;
            m_free      = nullptr;
            m_available = 0;
        }

        [[no_unique_address]] Upstream m_upstream {};
        std::byte*                     m_base {nullptr};
        FreeNode*                      m_free {nullptr};
        std::size_t                    m_available {0};
        std::size_t                    m_invalidDeallocations {0};
        std::array<bool, BlockCount>   m_allocated {};
    };
}// namespace NGIN::Memory

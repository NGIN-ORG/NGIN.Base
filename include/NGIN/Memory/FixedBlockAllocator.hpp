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
    /// @brief Owns a fixed slab divided into equal-size allocation blocks.
    /// @tparam BlockSize Maximum requested bytes per block.
    /// @tparam BlockCount Number of blocks in the slab.
    /// @tparam Alignment Alignment shared by every block.
    /// @tparam Upstream Allocator used to acquire the slab.
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
        /// @brief Acquires and initializes the fixed slab from an upstream allocator.
        /// @param upstream Allocator stored by the pool.
        explicit FixedBlockAllocator(Upstream upstream = {})
            : m_upstream(std::move(upstream))
        {
            m_base = static_cast<std::byte*>(m_upstream.Allocate(SlabSize, Alignment));
            if (m_base)
                ResetFreeList();
        }

        /// @brief Fixed block allocators cannot be copied because they own a slab.
        FixedBlockAllocator(const FixedBlockAllocator&) = delete;

        /// @brief Fixed block allocators cannot be copy-assigned because they own a slab.
        auto operator=(const FixedBlockAllocator&) -> FixedBlockAllocator& = delete;

        /// @brief Transfers slab ownership from another allocator.
        FixedBlockAllocator(FixedBlockAllocator&& other) noexcept
            requires std::is_nothrow_move_constructible_v<Upstream>
            : m_upstream(std::move(other.m_upstream)), m_base(std::exchange(other.m_base, nullptr)), m_free(std::exchange(other.m_free, nullptr)), m_available(std::exchange(other.m_available, 0)), m_invalidDeallocations(std::exchange(other.m_invalidDeallocations, 0)), m_allocated(std::exchange(other.m_allocated, std::array<bool, BlockCount> {}))
        {
        }

        /// @brief Releases the current slab and transfers ownership from another allocator.
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

        /// @brief Releases the owned slab.
        ~FixedBlockAllocator() { Release(); }

        /// @brief Allocates one block when size and alignment fit this pool's class.
        /// @return Block address, or `nullptr` for an invalid request or exhausted pool.
        [[nodiscard]] void* Allocate(const std::size_t bytes, const std::size_t alignment) noexcept
        {
            if (bytes == 0 || bytes > BlockSize || alignment == 0 || alignment > Alignment ||
                (alignment & (alignment - 1)) != 0 || !m_free)
                return nullptr;

            FreeNode* node          = m_free;
            m_free                  = node->next;
            const std::size_t index = static_cast<std::size_t>(reinterpret_cast<std::byte*>(node) - m_base) / Stride;
            m_allocated[index]      = true;
            --m_available;
            return node;
        }

        /// @brief Returns a previously allocated block to the free list.
        /// @details Invalid, interior, or duplicate deallocations are ignored and counted.
        void Deallocate(void* pointer, std::size_t, std::size_t) noexcept
        {
            if (!pointer)
                return;
            if (!IsBlockStart(pointer))
            {
                ++m_invalidDeallocations;
                return;
            }

            const std::size_t index = static_cast<std::size_t>(static_cast<std::byte*>(pointer) - m_base) / Stride;
            if (!m_allocated[index])
            {
                ++m_invalidDeallocations;
                return;
            }

            FreeNode* node = static_cast<FreeNode*>(pointer);
            node->next     = m_free;
            m_free         = node;
            ++m_available;
            m_allocated[index] = false;
        }

        /// @brief Allocates one block and reports the pool's granted size and alignment.
        [[nodiscard]] MemoryBlock AllocateEx(const std::size_t bytes, const std::size_t alignment) noexcept
        {
            void* pointer = Allocate(bytes, alignment);
            return {pointer, pointer ? BlockSize : 0, pointer ? Alignment : 0};
        }

        /// @brief Returns whether an address lies anywhere within the owned slab.
        [[nodiscard]] bool Owns(const void* pointer) const noexcept
        {
            if (!m_base || !pointer)
                return false;
            const std::byte* bytes = static_cast<const std::byte*>(pointer);
            return bytes >= m_base && bytes < m_base + SlabSize;
        }

        /// @brief Returns whether an address is the start of one of this pool's blocks.
        [[nodiscard]] bool IsBlockStart(const void* pointer) const noexcept
        {
            if (!Owns(pointer))
                return false;
            return static_cast<std::size_t>(static_cast<const std::byte*>(pointer) - m_base) % Stride == 0;
        }

        /// @brief Returns the largest allocation request served by this pool.
        [[nodiscard]] static constexpr std::size_t MaxSize() noexcept { return BlockSize; }

        /// @brief Returns the nominal payload bytes available across free blocks.
        [[nodiscard]] std::size_t Remaining() const noexcept { return m_available * BlockSize; }

        /// @brief Returns the total number of blocks in the pool.
        [[nodiscard]] std::size_t Capacity() const noexcept { return BlockCount; }

        /// @brief Returns the number of free blocks.
        [[nodiscard]] std::size_t AvailableBlocks() const noexcept { return m_available; }

        /// @brief Returns the number of rejected invalid or duplicate deallocations.
        [[nodiscard]] std::size_t InvalidDeallocations() const noexcept { return m_invalidDeallocations; }

    private:
        void ResetFreeList() noexcept
        {
            m_free = nullptr;
            for (std::size_t index = BlockCount; index > 0; --index)
            {
                FreeNode* node = reinterpret_cast<FreeNode*>(m_base + (index - 1) * Stride);
                node->next     = m_free;
                m_free         = node;
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

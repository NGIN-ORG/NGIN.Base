/// @file SegregatedPoolAllocator.hpp
/// @brief Fixed-capacity segregated pools for common small allocation sizes.
#pragma once

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace NGIN::Memory
{
    /// @brief Owns fixed-capacity pools for the 16, 32, 64, 128, 256, and 512-byte size classes.
    /// @tparam BlocksPerClass Number of blocks reserved for each size class.
    /// @tparam Upstream Allocator used to acquire the combined slab.
    template<std::size_t BlocksPerClass = 64, AllocatorConcept Upstream = SystemAllocator>
    class SegregatedPoolAllocator
    {
        static_assert(BlocksPerClass > 0);

        struct FreeNode
        {
            FreeNode* next {nullptr};
        };

        struct ClassState
        {
            std::size_t                      blockSize {0};
            std::size_t                      offset {0};
            FreeNode*                        free {nullptr};
            std::size_t                      available {0};
            std::array<bool, BlocksPerClass> allocated {};
        };

        static constexpr std::size_t                Alignment = alignof(std::max_align_t);
        static constexpr std::array<std::size_t, 6> Sizes {16, 32, 64, 128, 256, 512};

        [[nodiscard]] static consteval std::size_t ComputeSlabSize()
        {
            std::size_t size = 0;
            for (const std::size_t blockSize: Sizes)
                size += blockSize * BlocksPerClass;
            return size;
        }

        static constexpr std::size_t SlabSize = ComputeSlabSize();

    public:
        /// @brief Acquires and initializes the combined slab from an upstream allocator.
        explicit SegregatedPoolAllocator(Upstream upstream = {})
            : m_upstream(std::move(upstream))
        {
            m_base = static_cast<std::byte*>(m_upstream.Allocate(SlabSize, Alignment));
            if (m_base)
                Initialize();
        }

        /// @brief Segregated pool allocators cannot be copied because they own a slab.
        SegregatedPoolAllocator(const SegregatedPoolAllocator&) = delete;

        /// @brief Segregated pool allocators cannot be copy-assigned because they own a slab.
        auto operator=(const SegregatedPoolAllocator&) -> SegregatedPoolAllocator& = delete;

        /// @brief Transfers slab ownership from another allocator.
        SegregatedPoolAllocator(SegregatedPoolAllocator&& other) noexcept
            requires std::is_nothrow_move_constructible_v<Upstream>
            : m_upstream(std::move(other.m_upstream)), m_base(std::exchange(other.m_base, nullptr)), m_classes(std::move(other.m_classes)), m_invalidDeallocations(std::exchange(other.m_invalidDeallocations, 0))
        {
        }

        /// @brief Releases the current slab and transfers ownership from another allocator.
        auto operator=(SegregatedPoolAllocator&& other) noexcept -> SegregatedPoolAllocator&
            requires std::is_nothrow_move_assignable_v<Upstream>
        {
            if (this != &other)
            {
                Release();
                m_upstream             = std::move(other.m_upstream);
                m_base                 = std::exchange(other.m_base, nullptr);
                m_classes              = std::move(other.m_classes);
                m_invalidDeallocations = std::exchange(other.m_invalidDeallocations, 0);
            }
            return *this;
        }

        /// @brief Releases the owned slab.
        ~SegregatedPoolAllocator()
        {
            Release();
        }

        /// @brief Allocates from the smallest size class that satisfies the request.
        /// @return Block address, or `nullptr` for an invalid request or exhausted matching classes.
        [[nodiscard]] void* Allocate(const std::size_t bytes, const std::size_t alignment) noexcept
        {
            if (bytes == 0 || alignment == 0 || alignment > Alignment || (alignment & (alignment - 1)) != 0)
                return nullptr;
            for (ClassState& state: m_classes)
            {
                if (bytes > state.blockSize)
                    continue;
                if (!state.free)
                    continue;
                FreeNode* node = state.free;
                state.free     = node->next;
                const std::size_t index =
                        static_cast<std::size_t>(reinterpret_cast<std::byte*>(node) - (m_base + state.offset)) /
                        state.blockSize;
                state.allocated[index] = true;
                --state.available;
                return node;
            }
            return nullptr;
        }

        /// @brief Returns a block to its size class.
        /// @details Foreign, interior, and duplicate deallocations are ignored and counted.
        void Deallocate(void* pointer, std::size_t, std::size_t) noexcept
        {
            if (!pointer)
                return;
            ClassState* state = ClassForPointer(pointer);
            if (!state)
            {
                ++m_invalidDeallocations;
                return;
            }
            const std::size_t index =
                    static_cast<std::size_t>(static_cast<std::byte*>(pointer) - (m_base + state->offset)) /
                    state->blockSize;
            if (!state->allocated[index])
            {
                ++m_invalidDeallocations;
                return;
            }
            FreeNode* node = static_cast<FreeNode*>(pointer);
            node->next     = state->free;
            state->free    = node;
            ++state->available;
            state->allocated[index] = false;
        }

        /// @brief Allocates one block and reports its size-class capacity.
        [[nodiscard]] MemoryBlock AllocateEx(const std::size_t bytes, const std::size_t alignment) noexcept
        {
            void* pointer = Allocate(bytes, alignment);
            if (!pointer)
                return {};
            const ClassState* state = ClassForPointer(pointer);
            return {pointer, state ? state->blockSize : bytes, Alignment};
        }

        /// @brief Returns whether a pointer is the start of a block in any size class.
        [[nodiscard]] bool Owns(const void* pointer) const noexcept
        {
            return ClassForPointer(pointer) != nullptr;
        }

        /// @brief Returns the largest request served by the allocator.
        [[nodiscard]] static constexpr std::size_t MaxSize() noexcept { return Sizes.back(); }

        /// @brief Returns the total payload bytes remaining across every size class.
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            std::size_t remaining = 0;
            for (const ClassState& state: m_classes)
                remaining += state.available * state.blockSize;
            return remaining;
        }

        /// @brief Returns the number of rejected invalid or duplicate deallocations.
        [[nodiscard]] std::size_t InvalidDeallocations() const noexcept { return m_invalidDeallocations; }

    private:
        void Initialize() noexcept
        {
            std::size_t offset = 0;
            for (std::size_t classIndex = 0; classIndex < Sizes.size(); ++classIndex)
            {
                ClassState& state = m_classes[classIndex];
                state.blockSize   = Sizes[classIndex];
                state.offset      = offset;
                state.available   = BlocksPerClass;
                state.free        = nullptr;
                state.allocated.fill(false);
                for (std::size_t index = BlocksPerClass; index > 0; --index)
                {
                    FreeNode* node = reinterpret_cast<FreeNode*>(m_base + offset + (index - 1) * state.blockSize);
                    node->next     = state.free;
                    state.free     = node;
                }
                offset += state.blockSize * BlocksPerClass;
            }
        }

        [[nodiscard]] ClassState* ClassForPointer(const void* pointer) noexcept
        {
            return const_cast<ClassState*>(std::as_const(*this).ClassForPointer(pointer));
        }

        void Release() noexcept
        {
            if (m_base)
                m_upstream.Deallocate(m_base, SlabSize, Alignment);
            m_base    = nullptr;
            m_classes = {};
        }

        [[nodiscard]] const ClassState* ClassForPointer(const void* pointer) const noexcept
        {
            if (!m_base || !pointer)
                return nullptr;
            const std::byte* bytes = static_cast<const std::byte*>(pointer);
            for (const ClassState& state: m_classes)
            {
                const std::byte* begin = m_base + state.offset;
                const std::byte* end   = begin + state.blockSize * BlocksPerClass;
                if (bytes >= begin && bytes < end &&
                    static_cast<std::size_t>(bytes - begin) % state.blockSize == 0)
                    return &state;
            }
            return nullptr;
        }

        [[no_unique_address]] Upstream       m_upstream {};
        std::byte*                           m_base {nullptr};
        std::array<ClassState, Sizes.size()> m_classes {};
        std::size_t                          m_invalidDeallocations {0};
    };
}// namespace NGIN::Memory

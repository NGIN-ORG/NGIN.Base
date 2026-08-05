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
        explicit SegregatedPoolAllocator(Upstream upstream = {})
            : m_upstream(std::move(upstream))
        {
            m_base = static_cast<std::byte*>(m_upstream.Allocate(SlabSize, Alignment));
            if (m_base)
                Initialize();
        }

        SegregatedPoolAllocator(const SegregatedPoolAllocator&)                    = delete;
        auto operator=(const SegregatedPoolAllocator&) -> SegregatedPoolAllocator& = delete;

        SegregatedPoolAllocator(SegregatedPoolAllocator&& other) noexcept
            requires std::is_nothrow_move_constructible_v<Upstream>
            : m_upstream(std::move(other.m_upstream)), m_base(std::exchange(other.m_base, nullptr)), m_classes(std::move(other.m_classes)), m_invalidDeallocations(std::exchange(other.m_invalidDeallocations, 0))
        {
        }

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

        ~SegregatedPoolAllocator()
        {
            Release();
        }

        [[nodiscard]] void* Allocate(const std::size_t bytes, const std::size_t alignment) noexcept
        {
            if (bytes == 0 || alignment == 0 || alignment > Alignment || (alignment & (alignment - 1)) != 0)
                return nullptr;
            for (auto& state: m_classes)
            {
                if (bytes > state.blockSize)
                    continue;
                if (!state.free)
                    continue;
                FreeNode* node         = state.free;
                state.free             = node->next;
                const auto index       = static_cast<std::size_t>(reinterpret_cast<std::byte*>(node) - (m_base + state.offset)) /
                                         state.blockSize;
                state.allocated[index] = true;
                --state.available;
                return node;
            }
            return nullptr;
        }

        void Deallocate(void* pointer, std::size_t, std::size_t) noexcept
        {
            if (!pointer)
                return;
            auto* state = ClassForPointer(pointer);
            if (!state)
            {
                ++m_invalidDeallocations;
                return;
            }
            const auto index = static_cast<std::size_t>(static_cast<std::byte*>(pointer) - (m_base + state->offset)) /
                               state->blockSize;
            if (!state->allocated[index])
            {
                ++m_invalidDeallocations;
                return;
            }
            auto* node  = static_cast<FreeNode*>(pointer);
            node->next  = state->free;
            state->free = node;
            ++state->available;
            state->allocated[index] = false;
        }

        [[nodiscard]] MemoryBlock AllocateEx(const std::size_t bytes, const std::size_t alignment) noexcept
        {
            void* pointer = Allocate(bytes, alignment);
            if (!pointer)
                return {};
            const auto* state = ClassForPointer(pointer);
            return {pointer, state ? state->blockSize : bytes, Alignment};
        }

        [[nodiscard]] bool Owns(const void* pointer) const noexcept
        {
            return ClassForPointer(pointer) != nullptr;
        }

        [[nodiscard]] static constexpr std::size_t MaxSize() noexcept { return Sizes.back(); }

        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            std::size_t remaining = 0;
            for (const auto& state: m_classes)
                remaining += state.available * state.blockSize;
            return remaining;
        }

        [[nodiscard]] std::size_t InvalidDeallocations() const noexcept { return m_invalidDeallocations; }

    private:
        void Initialize() noexcept
        {
            std::size_t offset = 0;
            for (std::size_t classIndex = 0; classIndex < Sizes.size(); ++classIndex)
            {
                auto& state     = m_classes[classIndex];
                state.blockSize = Sizes[classIndex];
                state.offset    = offset;
                state.available = BlocksPerClass;
                state.free      = nullptr;
                state.allocated.fill(false);
                for (std::size_t index = BlocksPerClass; index > 0; --index)
                {
                    auto* node = reinterpret_cast<FreeNode*>(m_base + offset + (index - 1) * state.blockSize);
                    node->next = state.free;
                    state.free = node;
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
            const auto* bytes = static_cast<const std::byte*>(pointer);
            for (const auto& state: m_classes)
            {
                const auto* begin = m_base + state.offset;
                const auto* end   = begin + state.blockSize * BlocksPerClass;
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

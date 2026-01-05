/// @file BufferPool.hpp
/// @brief Simple pool for network buffers.
#pragma once

#include <cstddef>
#include <vector>

#include <type_traits>
#include <utility>

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Net/Types/Buffer.hpp>

namespace NGIN::Net
{
    /// @brief Non-thread-safe pool for reusable byte buffers.
    ///
    /// Buffers must not outlive the pool that created them.
    template<NGIN::Memory::AllocatorConcept Allocator = NGIN::Memory::SystemAllocator>
    class BufferPool final
    {
    public:
        constexpr BufferPool() noexcept(std::is_nothrow_default_constructible_v<Allocator>) = default;

        explicit BufferPool(Allocator allocator) noexcept(std::is_nothrow_move_constructible_v<Allocator>)
            : m_allocator(std::move(allocator))
        {
        }

        BufferPool(const BufferPool&)            = delete;
        BufferPool& operator=(const BufferPool&) = delete;
        BufferPool(BufferPool&&)                 = delete;
        BufferPool& operator=(BufferPool&&)      = delete;

        ~BufferPool() { Clear(); }

        Buffer Rent(NGIN::UInt32 minimumCapacity)
        {
            if (minimumCapacity == 0)
            {
                return {};
            }

            for (std::size_t i = 0; i < m_free.size(); ++i)
            {
                if (m_free[i].capacity >= minimumCapacity)
                {
                    const auto block = m_free[i];
                    m_free[i]        = m_free.back();
                    m_free.pop_back();
                    return MakeBuffer(block);
                }
            }

            auto* mem = static_cast<NGIN::Byte*>(
                    m_allocator.Allocate(static_cast<std::size_t>(minimumCapacity), BufferAlignment));
            if (!mem)
            {
                return {};
            }

            return MakeBuffer(Block {mem, minimumCapacity});
        }

        void Clear() noexcept
        {
            for (const auto& block: m_free)
            {
                m_allocator.Deallocate(block.data, block.capacity, BufferAlignment);
            }
            m_free.clear();
        }

    private:
        static constexpr std::size_t BufferAlignment = alignof(std::max_align_t);

        struct Block final
        {
            NGIN::Byte*  data {nullptr};
            NGIN::UInt32 capacity {0};
        };

        static void ReleaseToPool(void* owner, Buffer& buffer) noexcept
        {
            auto* pool = static_cast<BufferPool*>(owner);
            if (!pool || !buffer.data || buffer.capacity == 0)
            {
                return;
            }
            try
            {
                pool->m_free.push_back(Block {buffer.data, buffer.capacity});
            }
            catch (...)
            {
                pool->m_allocator.Deallocate(buffer.data, buffer.capacity, BufferAlignment);
            }
        }

        Buffer MakeBuffer(const Block& block) noexcept
        {
            Buffer buffer {};
            buffer.data      = block.data;
            buffer.capacity  = block.capacity;
            buffer.size      = 0;
            buffer.m_owner   = this;
            buffer.m_release = &BufferPool::ReleaseToPool;
            return buffer;
        }

        [[no_unique_address]] Allocator m_allocator {};
        std::vector<Block> m_free {};
    };
}// namespace NGIN::Net

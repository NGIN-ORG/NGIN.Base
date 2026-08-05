#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Memory/PolyAllocatorRef.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>

namespace NGIN::Serialization
{
    /// @brief Growable monotonic arena with stable allocations and an explicit byte limit.
    class SegmentedArena
    {
    public:
        explicit SegmentedArena(UIntSize maxCommittedBytes,
                                UIntSize initialBlockBytes = 4096)
            : m_upstream(m_systemAllocator),
              m_blocks(0, m_upstream),
              m_maxCommittedBytes(maxCommittedBytes),
              m_nextBlockBytes((std::max) (initialBlockBytes, UIntSize {256}))
        {
        }

        explicit SegmentedArena(NGIN::Memory::PolyAllocatorRef upstream,
                                UIntSize                       maxCommittedBytes,
                                UIntSize                       initialBlockBytes = 4096)
            : m_upstream(upstream ? upstream : NGIN::Memory::PolyAllocatorRef {m_systemAllocator}),
              m_blocks(0, m_upstream),
              m_maxCommittedBytes(maxCommittedBytes),
              m_nextBlockBytes((std::max) (initialBlockBytes, UIntSize {256}))
        {
        }

        SegmentedArena(const SegmentedArena&)            = delete;
        SegmentedArena& operator=(const SegmentedArena&) = delete;
        SegmentedArena(SegmentedArena&&)                 = delete;
        SegmentedArena& operator=(SegmentedArena&&)      = delete;

        ~SegmentedArena()
        {
            for (UIntSize index = 0; index < m_blocks.Size(); ++index)
            {
                auto& block = m_blocks[index];
                m_upstream.Deallocate(block.data, block.capacity, alignof(std::max_align_t));
            }
        }

        [[nodiscard]] void* Allocate(UIntSize size, UIntSize alignment) noexcept
        {
            if (size == 0)
                return nullptr;

            const UIntSize normalizedAlignment = NormalizeAlignment(alignment);
            if (m_blocks.Size() > 0)
            {
                if (void* memory = TryAllocate(m_blocks[m_blocks.Size() - 1], size, normalizedAlignment))
                    return memory;
            }

            const UIntSize required = size > (std::numeric_limits<UIntSize>::max)() - normalizedAlignment
                                              ? (std::numeric_limits<UIntSize>::max)()
                                              : size + normalizedAlignment;
            if (m_committedBytes > m_maxCommittedBytes)
                return nullptr;

            const UIntSize remainingBytes = m_maxCommittedBytes - m_committedBytes;
            UIntSize       blockBytes     = (std::max) (m_nextBlockBytes, required);
            if (blockBytes > remainingBytes)
            {
                blockBytes = required;
                if (blockBytes > remainingBytes)
                    return nullptr;
            }

            void* data = m_upstream.Allocate(blockBytes, alignof(std::max_align_t));
            if (!data)
                return nullptr;

            try
            {
                m_blocks.PushBack(Block {
                        .data     = static_cast<std::byte*>(data),
                        .capacity = blockBytes,
                        .used     = 0,
                });
            } catch (...)
            {
                m_upstream.Deallocate(data, blockBytes, alignof(std::max_align_t));
                return nullptr;
            }

            m_committedBytes += blockBytes;
            m_nextBlockBytes = blockBytes <= (std::numeric_limits<UIntSize>::max)() / 2
                                       ? blockBytes * 2
                                       : blockBytes;
            return TryAllocate(m_blocks[m_blocks.Size() - 1], size, normalizedAlignment);
        }

        [[nodiscard]] std::string_view CopyString(std::string_view value) noexcept
        {
            if (value.empty())
                return {};
            void* memory = Allocate(value.size(), alignof(char));
            if (!memory)
                return {};
            std::memcpy(memory, value.data(), value.size());
            return {static_cast<const char*>(memory), value.size()};
        }

        [[nodiscard]] UIntSize CommittedBytes() const noexcept { return m_committedBytes; }
        [[nodiscard]] UIntSize UsedBytes() const noexcept { return m_usedBytes; }
        [[nodiscard]] UIntSize MaxCommittedBytes() const noexcept { return m_maxCommittedBytes; }

    private:
        struct Block
        {
            std::byte* data {nullptr};
            UIntSize   capacity {0};
            UIntSize   used {0};
        };

        [[nodiscard]] static constexpr UIntSize NormalizeAlignment(UIntSize value) noexcept
        {
            if (value <= 1)
                return 1;
            --value;
            for (UIntSize shift = 1; shift < sizeof(UIntSize) * 8; shift <<= 1)
                value |= value >> shift;
            return value + 1;
        }

        [[nodiscard]] void* TryAllocate(Block& block, UIntSize size, UIntSize alignment) noexcept
        {
            const UIntSize mask = alignment - 1;
            if (block.used > (std::numeric_limits<UIntSize>::max)() - mask)
                return nullptr;
            const UIntSize aligned = (block.used + mask) & ~mask;
            if (aligned > block.capacity || size > block.capacity - aligned)
                return nullptr;
            void* memory = block.data + aligned;
            block.used   = aligned + size;
            m_usedBytes += size;
            return memory;
        }

        [[no_unique_address]] NGIN::Memory::SystemAllocator             m_systemAllocator {};
        NGIN::Memory::PolyAllocatorRef                                  m_upstream {};
        NGIN::Containers::Vector<Block, NGIN::Memory::PolyAllocatorRef> m_blocks;
        UIntSize                                                        m_maxCommittedBytes {0};
        UIntSize                                                        m_nextBlockBytes {4096};
        UIntSize                                                        m_committedBytes {0};
        UIntSize                                                        m_usedBytes {0};
    };
}// namespace NGIN::Serialization

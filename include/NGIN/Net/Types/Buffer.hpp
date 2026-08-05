/// @file Buffer.hpp
/// @brief Byte buffer with optional pool-backed ownership.
#pragma once

#include <span>

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    template<NGIN::Memory::AllocatorConcept Allocator>
    class BufferPool;

    using ByteSpan      = std::span<NGIN::Byte>;
    using ConstByteSpan = std::span<const NGIN::Byte>;

    /// @brief Segment of a payload.
    struct BufferSegment final
    {
        const NGIN::Byte* data {nullptr};
        NGIN::UInt32      size {0};
    };

    using BufferSegmentSpan = std::span<const BufferSegment>;

    /// @brief Mutable payload segment.
    struct MutableBufferSegment final
    {
        NGIN::Byte*  data {nullptr};
        NGIN::UInt32 size {0};
    };

    using MutableBufferSegmentSpan = std::span<MutableBufferSegment>;

    using IOVec            = BufferSegment;
    using IOVecSpan        = BufferSegmentSpan;
    using MutableIOVec     = MutableBufferSegment;
    using MutableIOVecSpan = MutableBufferSegmentSpan;

    /// @brief Move-only buffer that can return to a BufferPool.
    struct Buffer final
    {
        using ReleaseFn = void (*)(void*, Buffer&) noexcept;

        NGIN::Byte*  data {nullptr};
        NGIN::UInt32 size {0};
        NGIN::UInt32 capacity {0};

        /// @brief Constructs an empty buffer.
        constexpr Buffer() noexcept = default;

        /// @brief Transfers data and pool-return ownership from another buffer.
        Buffer(Buffer&& other) noexcept { MoveFrom(std::move(other)); }

        /// @brief Releases this buffer and transfers ownership from another buffer.
        Buffer& operator=(Buffer&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        /// @brief Buffers are non-copyable because they uniquely own their release callback.
        Buffer(const Buffer&) = delete;
        /// @brief Buffers are non-copy-assignable because they uniquely own their release callback.
        Buffer& operator=(const Buffer&) = delete;

        /// @brief Returns owned storage to its pool or allocator.
        ~Buffer() { Release(); }

        /// @brief Returns whether this buffer contains storage.
        [[nodiscard]] constexpr bool IsValid() const noexcept { return data != nullptr; }

        /// @brief Returns storage through its release callback and resets this buffer.
        void Release() noexcept
        {
            if (m_release)
            {
                m_release(m_owner, *this);
            }
            Reset();
        }

    private:
        template<NGIN::Memory::AllocatorConcept Allocator>
        friend class BufferPool;

        void*     m_owner {nullptr};
        ReleaseFn m_release {nullptr};

        void Reset() noexcept
        {
            data      = nullptr;
            size      = 0;
            capacity  = 0;
            m_owner   = nullptr;
            m_release = nullptr;
        }

        void MoveFrom(Buffer&& other) noexcept
        {
            data      = other.data;
            size      = other.size;
            capacity  = other.capacity;
            m_owner   = other.m_owner;
            m_release = other.m_release;
            other.Reset();
        }
    };
}// namespace NGIN::Net

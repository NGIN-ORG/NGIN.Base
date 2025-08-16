/// @file BumpArena.hpp
/// @brief Linear (bump-pointer) arena allocator with optional owned buffer and rollback markers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <new>
#include <algorithm>

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

namespace NGIN::Memory
{
    // Removed tag-based owned/borrowed; arena is always owning its buffer allocated from upstream.

    /// @brief Simple bump-pointer arena. Not thread-safe. Deallocate is a no-op.
    template<class Upstream = SystemAllocator>
    class BumpArena
    {
    public:
        using UpstreamAllocator = Upstream;

        BumpArena() = delete;

        /// @brief Construct an owning arena allocating memory from upstream.
        explicit BumpArena(std::size_t capacity, Upstream upstream = {})
            : m_upstream(std::move(upstream)), m_base(static_cast<std::byte*>(m_upstream.Allocate(capacity, alignof(std::max_align_t)))), m_current(m_base), m_capacity(capacity) {}

        /// @brief Factory for clarity.
        static BumpArena Create(std::size_t capacity, Upstream upstream = {})
        {
            return BumpArena(capacity, std::move(upstream));
        }

        BumpArena(const BumpArena&)            = delete;
        BumpArena& operator=(const BumpArena&) = delete;

        BumpArena(BumpArena&& other) noexcept
            : m_upstream(std::move(other.m_upstream)), m_base(other.m_base), m_current(other.m_current), m_capacity(other.m_capacity)
        {
            other.m_base = other.m_current = nullptr;
            other.m_capacity               = 0;
        }
        BumpArena& operator=(BumpArena&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                m_upstream   = std::move(other.m_upstream);
                m_base       = other.m_base;
                m_current    = other.m_current;
                m_capacity   = other.m_capacity;
                other.m_base = other.m_current = nullptr;
                other.m_capacity               = 0;
            }
            return *this;
        }

        ~BumpArena()
        {
            Release();
        }

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept
        {
            if (size == 0 || !m_base)
                return nullptr;
            if (alignment == 0)
                alignment = 1;
            std::uintptr_t currentAddr = reinterpret_cast<std::uintptr_t>(m_current);
            std::uintptr_t aligned     = (currentAddr + (alignment - 1)) & ~(static_cast<std::uintptr_t>(alignment) - 1);
            std::size_t padding        = static_cast<std::size_t>(aligned - currentAddr);
            if (padding > Remaining())
                return nullptr;
            if (size > (m_capacity - Used() - padding))
                return nullptr;
            std::byte* userPtr = reinterpret_cast<std::byte*>(aligned);
            m_current          = userPtr + size;
            return userPtr;
        }

        void Deallocate(void*, std::size_t, std::size_t) noexcept
        { /* no-op */
        }

        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            return m_capacity;
        }
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            return m_capacity - Used();
        }
        [[nodiscard]] std::size_t Used() const noexcept
        {
            return static_cast<std::size_t>(m_current - m_base);
        }
        [[nodiscard]] bool Owns(const void* p) const noexcept
        {
            auto addr = reinterpret_cast<const std::byte*>(p);
            return addr >= m_base && addr < m_base + m_capacity;
        }

        void Reset() noexcept
        {
            m_current = m_base;
        }

        [[nodiscard]] ArenaMarker Mark() const noexcept
        {
            return {m_current};
        }
        void Rollback(ArenaMarker m) noexcept
        {
            if (m.ptr >= m_base && m.ptr <= m_base + m_capacity)
                m_current = static_cast<std::byte*>(m.ptr);
        }

    private:
        void Release() noexcept
        {
            if (m_base)
            {
                m_upstream.Deallocate(m_base, m_capacity, alignof(std::max_align_t));
            }
            m_base = m_current = nullptr;
            m_capacity         = 0;
        }

        [[no_unique_address]] Upstream m_upstream {};
        std::byte* m_base {nullptr};
        std::byte* m_current {nullptr};
        std::size_t m_capacity {0};
    };

}// namespace NGIN::Memory

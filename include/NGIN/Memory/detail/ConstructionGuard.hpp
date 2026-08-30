/// @file ConstructionGuard.hpp
/// @brief Rollback guards for uncommitted allocations and object construction.
#pragma once

#include <NGIN/Memory/AllocatorConcept.hpp>

#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace NGIN::Memory::detail
{
    /// @brief Owns an allocator block until an operation commits it.
    template<AllocatorConcept Allocator>
    class AllocationGuard final
    {
    public:
        /// @brief Takes temporary ownership of an allocated block.
        AllocationGuard(
                Allocator&        allocator,
                void*             pointer,
                const std::size_t size,
                const std::size_t alignment) noexcept
            : m_allocator(&allocator), m_pointer(pointer), m_size(size), m_alignment(alignment)
        {
        }

        AllocationGuard(const AllocationGuard&)            = delete;
        AllocationGuard& operator=(const AllocationGuard&) = delete;

        /// @brief Releases the block unless ownership was committed.
        ~AllocationGuard()
        {
            if (m_pointer)
                m_allocator->Deallocate(m_pointer, m_size, m_alignment);
        }

        /// @brief Relinquishes ownership after a successful commit.
        void Release() noexcept
        {
            m_pointer = nullptr;
        }

    private:
        Allocator*  m_allocator;
        void*       m_pointer;
        std::size_t m_size;
        std::size_t m_alignment;
    };

    /// @brief Destroys a contiguous prefix of newly constructed objects unless committed.
    template<class T>
        requires std::destructible<T>
    class ConstructionGuard final
    {
    public:
        /// @brief Begins tracking construction at `begin`.
        explicit ConstructionGuard(T* begin) noexcept
            : m_begin(begin)
        {
        }

        ConstructionGuard(const ConstructionGuard&)            = delete;
        ConstructionGuard& operator=(const ConstructionGuard&) = delete;

        /// @brief Destroys tracked objects in reverse construction order.
        ~ConstructionGuard()
        {
            while (m_count > 0)
            {
                --m_count;
                std::destroy_at(m_begin + m_count);
            }
        }

        /// @brief Records one successfully constructed object at the end of the prefix.
        void Increment() noexcept
        {
            ++m_count;
        }

        /// @brief Returns the number of currently tracked objects.
        [[nodiscard]] std::size_t Count() const noexcept
        {
            return m_count;
        }

        /// @brief Relinquishes responsibility after the objects are committed.
        void Release() noexcept
        {
            m_count = 0;
        }

    private:
        T*          m_begin;
        std::size_t m_count {0};
    };

    /// @brief Destroys one newly constructed object unless committed.
    template<class T>
        requires std::destructible<T>
    class ObjectConstructionGuard final
    {
    public:
        /// @brief Creates an inactive guard for an object address.
        explicit ObjectConstructionGuard(T* object) noexcept
            : m_object(object)
        {
        }

        ObjectConstructionGuard(const ObjectConstructionGuard&)            = delete;
        ObjectConstructionGuard& operator=(const ObjectConstructionGuard&) = delete;

        /// @brief Destroys the object when construction was recorded but not committed.
        ~ObjectConstructionGuard()
        {
            if (m_constructed)
                std::destroy_at(m_object);
        }

        /// @brief Records successful construction of the guarded object.
        void MarkConstructed() noexcept
        {
            m_constructed = true;
        }

        /// @brief Relinquishes responsibility after the object is committed.
        void Release() noexcept
        {
            m_constructed = false;
        }

    private:
        T*   m_object;
        bool m_constructed {false};
    };
}// namespace NGIN::Memory::detail

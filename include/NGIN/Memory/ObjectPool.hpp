/// @file ObjectPool.hpp
/// @brief Typed fixed-capacity object pool with constructor rollback.
#pragma once

#include <NGIN/Memory/FixedBlockAllocator.hpp>

#include <algorithm>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace NGIN::Memory
{
    /// @brief Fixed-capacity pool that owns storage for objects of one type.
    /// @tparam T Object type.
    /// @tparam Capacity Maximum number of simultaneously allocated objects.
    /// @tparam Upstream Allocator used for the pool's backing blocks.
    template<class T, std::size_t Capacity, AllocatorConcept Upstream = SystemAllocator>
    class ObjectPool
    {
        static constexpr std::size_t Alignment = (std::max) (alignof(T), alignof(void*));
        using Storage                          = FixedBlockAllocator<sizeof(T), Capacity, Alignment, Upstream>;

    public:
        /// @brief Constructs an empty pool with an upstream allocator.
        explicit ObjectPool(Upstream upstream = {})
            : m_storage(std::move(upstream))
        {
        }

        /// @brief Allocates a slot and constructs an object in place.
        /// @param args Arguments forwarded to `T`'s constructor.
        /// @return Constructed object, or `nullptr` when the pool is exhausted.
        template<class... Args>
        [[nodiscard]] T* Create(Args&&... args)
        {
            void* storage = m_storage.Allocate(sizeof(T), alignof(T));
            if (!storage)
                return nullptr;
            try
            {
                return ::new (storage) T(std::forward<Args>(args)...);
            } catch (...)
            {
                m_storage.Deallocate(storage, sizeof(T), alignof(T));
                throw;
            }
        }

        /// @brief Destroys an object owned by the pool and releases its slot.
        /// @param object Object returned by `Create`; null and foreign pointers are ignored.
        void Destroy(T* object) noexcept(std::is_nothrow_destructible_v<T>)
        {
            if (!object || !m_storage.Owns(object))
                return;
            object->~T();
            m_storage.Deallocate(object, sizeof(T), alignof(T));
        }

        /// @brief Returns whether an address belongs to the pool's storage.
        [[nodiscard]] bool Owns(const T* object) const noexcept { return m_storage.Owns(object); }

        /// @brief Returns the number of currently available object slots.
        [[nodiscard]] std::size_t Available() const noexcept { return m_storage.AvailableBlocks(); }

        /// @brief Returns the compile-time maximum number of objects.
        [[nodiscard]] static constexpr std::size_t MaxObjects() noexcept { return Capacity; }

    private:
        Storage m_storage;
    };
}// namespace NGIN::Memory

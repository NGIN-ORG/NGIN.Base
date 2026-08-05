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
    template<class T, std::size_t Capacity, AllocatorConcept Upstream = SystemAllocator>
    class ObjectPool
    {
        static constexpr std::size_t Alignment = (std::max) (alignof(T), alignof(void*));
        using Storage                          = FixedBlockAllocator<sizeof(T), Capacity, Alignment, Upstream>;

    public:
        explicit ObjectPool(Upstream upstream = {})
            : m_storage(std::move(upstream))
        {
        }

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

        void Destroy(T* object) noexcept(std::is_nothrow_destructible_v<T>)
        {
            if (!object || !m_storage.Owns(object))
                return;
            object->~T();
            m_storage.Deallocate(object, sizeof(T), alignof(T));
        }

        [[nodiscard]] bool                         Owns(const T* object) const noexcept { return m_storage.Owns(object); }
        [[nodiscard]] std::size_t                  Available() const noexcept { return m_storage.AvailableBlocks(); }
        [[nodiscard]] static constexpr std::size_t MaxObjects() noexcept { return Capacity; }

    private:
        Storage m_storage;
    };
}// namespace NGIN::Memory

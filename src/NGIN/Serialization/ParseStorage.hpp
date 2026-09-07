#pragma once
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <algorithm>
#include <limits>
#include <new>
#include <vector>
namespace NGIN::Serialization::detail
{
    class AllocationBudget
    {
    public:
        explicit AllocationBudget(NGIN::Memory::PolyAllocatorRef upstream,
                                  UIntSize                       maxCommittedBytes) noexcept
            : m_upstream(upstream ? upstream
                                  : NGIN::Memory::PolyAllocatorRef {m_systemAllocator}),
              m_maxCommittedBytes(maxCommittedBytes)
        {
        }

        [[nodiscard]] void* Allocate(UIntSize size, UIntSize alignment) noexcept
        {
            if (size == 0)
                return nullptr;
            if (size > Remaining())
            {
                m_limitExceeded = true;
                return nullptr;
            }
            void* memory = m_upstream.Allocate(size, alignment);
            if (!memory)
                return nullptr;
            m_committedBytes += size;
            m_peakCommittedBytes = (std::max) (m_peakCommittedBytes, m_committedBytes);
            ++m_allocationCount;
            return memory;
        }

        void Deallocate(void* memory, UIntSize size, UIntSize alignment) noexcept
        {
            if (!memory)
                return;
            m_upstream.Deallocate(memory, size, alignment);
            m_committedBytes = size <= m_committedBytes ? m_committedBytes - size : 0;
        }

        [[nodiscard]] UIntSize MaxSize() const noexcept { return m_maxCommittedBytes; }
        [[nodiscard]] UIntSize Remaining() const noexcept
        {
            return m_committedBytes <= m_maxCommittedBytes
                           ? m_maxCommittedBytes - m_committedBytes - m_externalBytes
                           : 0;
        }
        [[nodiscard]] UIntSize CommittedBytes() const noexcept { return m_committedBytes; }
        [[nodiscard]] UIntSize PeakCommittedBytes() const noexcept { return m_peakCommittedBytes; }
        [[nodiscard]] UIntSize AllocationCount() const noexcept { return m_allocationCount; }
        [[nodiscard]] bool     LimitExceeded() const noexcept { return m_limitExceeded; }
        // Charge externally owned scratch capacity to the same retained-storage limit.
        [[nodiscard]] bool SetExternalBytes(UIntSize bytes) noexcept
        {
            if (bytes > m_maxCommittedBytes - m_committedBytes)
            {
                m_limitExceeded = true;
                return false;
            }
            m_externalBytes = bytes;
            return true;
        }
        void ResetFailure() noexcept { m_limitExceeded = false; }

    private:
        NGIN::Memory::SystemAllocator  m_systemAllocator {};
        NGIN::Memory::PolyAllocatorRef m_upstream {};
        UIntSize                       m_maxCommittedBytes {0};
        UIntSize                       m_committedBytes {0};
        UIntSize                       m_externalBytes {0};
        UIntSize                       m_peakCommittedBytes {0};
        UIntSize                       m_allocationCount {0};
        bool                           m_limitExceeded {false};
    };

    template<class T>
    class BudgetAllocator
    {
    public:
        using value_type = T;

        BudgetAllocator() noexcept = default;
        explicit BudgetAllocator(AllocationBudget& budget) noexcept
            : m_budget(&budget)
        {
        }

        template<class U>
        BudgetAllocator(const BudgetAllocator<U>& other) noexcept
            : m_budget(other.Budget())
        {
        }

        [[nodiscard]] T* allocate(std::size_t count)
        {
            if (!m_budget || count > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
                throw std::bad_alloc {};
            void* memory = m_budget->Allocate(count * sizeof(T), alignof(T));
            if (!memory)
                throw std::bad_alloc {};
            return static_cast<T*>(memory);
        }

        void deallocate(T* memory, std::size_t count) noexcept
        {
            if (m_budget)
                m_budget->Deallocate(memory, count * sizeof(T), alignof(T));
        }

        [[nodiscard]] AllocationBudget* Budget() const noexcept { return m_budget; }

        template<class U>
        [[nodiscard]] bool operator==(const BudgetAllocator<U>& other) const noexcept
        {
            return m_budget == other.Budget();
        }

    private:
        AllocationBudget* m_budget {nullptr};
    };

    template<class T>
    using BudgetVector = std::vector<T, BudgetAllocator<T>>;

}// namespace NGIN::Serialization::detail

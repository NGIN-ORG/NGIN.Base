#pragma once
#include "ParseStorage.hpp"
#include <functional>
#include <string_view>

namespace NGIN::Serialization::detail
{
    // Indexes external immutable names by record ID. Generic key/value maps retain
    // a key and metadata per bucket; these slots need only four bytes each.
    class NameIndex
    {
    public:
        static constexpr UIntSize Threshold = 16;
        static constexpr UInt32   Missing   = static_cast<UInt32>(-1);
        explicit NameIndex(AllocationBudget& budget) : m_slots(BudgetAllocator<UInt32> {budget}) {}
        NameIndex(NameIndex&&) noexcept        = default;
        NameIndex& operator=(NameIndex&&)      = default;
        NameIndex(const NameIndex&)            = delete;
        NameIndex& operator=(const NameIndex&) = delete;

        template<class NameAt>
        [[nodiscard]] UInt32 Find(std::string_view name, UIntSize count, NameAt nameAt) const noexcept
        {
            if (m_slots.empty())
            {
                for (UIntSize i = 0; i < count; ++i)
                    if (nameAt(i) == name)
                        return static_cast<UInt32>(i);
                return Missing;
            }
            UIntSize slot = std::hash<std::string_view> {}(name) & (m_slots.size() - 1);
            while (m_slots[slot] != Missing)
            {
                if (nameAt(m_slots[slot]) == name)
                    return m_slots[slot];
                slot = (slot + 1) & (m_slots.size() - 1);
            }
            return Missing;
        }

        template<class NameAt>
        void Append(UIntSize count, NameAt nameAt)
        {
            if (count < Threshold)
                return;
            if (count > Missing || count > (std::numeric_limits<UIntSize>::max)() / 2)
                throw std::bad_alloc {};
            if (m_slots.size() < count * 2)
            {
                UIntSize capacity = 64;
                while (capacity < count * 2)
                {
                    if (capacity > (std::numeric_limits<UIntSize>::max)() / 2)
                        throw std::bad_alloc {};
                    capacity *= 2;
                }
                m_slots.assign(capacity, Missing);
                for (UIntSize i = 0; i < count; ++i)
                    Insert(static_cast<UInt32>(i), nameAt);
            }
            else
                Insert(static_cast<UInt32>(count - 1), nameAt);
        }
        [[nodiscard]] bool     Empty() const noexcept { return m_slots.empty(); }
        [[nodiscard]] UIntSize MemoryUsed() const noexcept { return m_slots.size() * sizeof(UInt32); }

    private:
        template<class NameAt>
        void Insert(UInt32 index, NameAt nameAt)
        {
            const auto name = nameAt(index);
            UIntSize   slot = std::hash<std::string_view> {}(name) & (m_slots.size() - 1);
            while (m_slots[slot] != Missing)
            {
                if (nameAt(m_slots[slot]) == name)
                    return;// Preserve first source-order match.
                slot = (slot + 1) & (m_slots.size() - 1);
            }
            m_slots[slot] = index;
        }
        BudgetVector<UInt32> m_slots;
    };
    struct IndexedNames
    {
        UIntSize  begin;
        NameIndex index;
    };
}// namespace NGIN::Serialization::detail

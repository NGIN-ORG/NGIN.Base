/// @file ConcurrentHashMapDetail.hpp
/// @brief Internal detail types for the concurrent hash map scaffold.
#pragma once

#include <NGIN/Defines.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <type_traits>
#include <utility>

namespace NGIN::Containers::detail
{
    // Included only from ConcurrentHashMap.hpp after ReclamationPolicy is declared.

    struct ConcurrentHashMapRetiredRecord
    {
        void* object {nullptr};
        void* context {nullptr};
        void (*deleter)(void*, void*) noexcept {nullptr};
        ConcurrentHashMapRetiredRecord* next {nullptr};
        std::uint64_t                   retireEpoch {0};
    };

    class ConcurrentHashMapRetireList
    {
    protected:
        void Retire(
                void* object,
                void* context,
                void (*deleter)(void*, void*) noexcept,
                const std::uint64_t retireEpoch = 0)
        {
            if (!object || !deleter)
            {
                return;
            }

            auto* record = new ConcurrentHashMapRetiredRecord {
                    .object      = object,
                    .context     = context,
                    .deleter     = deleter,
                    .next        = m_retiredHead,
                    .retireEpoch = retireEpoch,
            };
            m_retiredHead = record;
            m_pending.fetch_add(1, std::memory_order_relaxed);
        }

        template<class Predicate>
        void ReclaimIf(Predicate&& predicate) noexcept
        {
            ConcurrentHashMapRetiredRecord* current   = m_retiredHead;
            ConcurrentHashMapRetiredRecord* survivors = nullptr;
            m_retiredHead                             = nullptr;

            while (current)
            {
                ConcurrentHashMapRetiredRecord* next = current->next;
                if (predicate(*current))
                {
                    current->deleter(current->context, current->object);
                    delete current;
                    m_pending.fetch_sub(1, std::memory_order_relaxed);
                    m_reclaimed.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    current->next = survivors;
                    survivors     = current;
                }
                current = next;
            }
            m_retiredHead = survivors;
        }

        void ReclaimRetired() noexcept
        {
            ReclaimIf([](const ConcurrentHashMapRetiredRecord&) noexcept { return true; });
        }

        [[nodiscard]] std::size_t PendingRetired() const noexcept
        {
            return m_pending.load(std::memory_order_relaxed);
        }

        [[nodiscard]] std::size_t ReclaimedRetired() const noexcept
        {
            return m_reclaimed.load(std::memory_order_relaxed);
        }

        ConcurrentHashMapRetiredRecord* m_retiredHead {nullptr};
        std::atomic<std::size_t>        m_pending {0};
        std::atomic<std::size_t>        m_reclaimed {0};
    };

    template<ReclamationPolicy Policy>
    class ConcurrentHashMapReclaimer;

    template<>
    class ConcurrentHashMapReclaimer<ReclamationPolicy::LocalEpoch> : private ConcurrentHashMapRetireList
    {
        struct ReaderRecord
        {
            std::uint64_t epoch {0};
            ReaderRecord* next {nullptr};
        };

    public:
        class ReadGuard
        {
        public:
            ReadGuard() = default;

            explicit ReadGuard(ConcurrentHashMapReclaimer* owner) noexcept
                : m_owner(owner)
            {
                if (m_owner)
                    m_owner->Register(m_record);
            }

            ReadGuard(const ReadGuard&)                    = delete;
            auto operator=(const ReadGuard&) -> ReadGuard& = delete;
            ReadGuard(ReadGuard&&)                         = delete;
            auto operator=(ReadGuard&&) -> ReadGuard&      = delete;

            ~ReadGuard()
            {
                Release();
            }

        private:
            void Release() noexcept
            {
                if (m_owner)
                {
                    m_owner->Unregister(m_record);
                    m_owner = nullptr;
                }
            }

            ConcurrentHashMapReclaimer* m_owner {nullptr};
            ReaderRecord                m_record {};
        };

        [[nodiscard]] auto Enter() const noexcept -> ReadGuard
        {
            return ReadGuard(const_cast<ConcurrentHashMapReclaimer*>(this));
        }

        template<class T>
        [[nodiscard]] auto Protect(const std::atomic<T*>& pointer, ReadGuard&) const noexcept -> T*
        {
            return pointer.load(std::memory_order_acquire);
        }

        template<class T>
        [[nodiscard]] auto Protect(const std::atomic<T*>& pointer, ReadGuard&) noexcept -> T*
        {
            return pointer.load(std::memory_order_acquire);
        }

        void Retire(void* object, void* context, void (*deleter)(void*, void*) noexcept)
        {
            ConcurrentHashMapRetireList::Retire(
                    object, context, deleter, m_epoch.load(std::memory_order_acquire));
        }

        void Poll() noexcept
        {
            (void) m_epoch.fetch_add(1, std::memory_order_acq_rel);
            const std::uint64_t minimum = MinimumActiveEpoch();
            ConcurrentHashMapRetireList::ReclaimIf(
                    [minimum](const ConcurrentHashMapRetiredRecord& record) noexcept {
                        return record.retireEpoch < minimum;
                    });
        }

        void Quiesce() noexcept
        {
            while (ActiveReaders() != 0)
            {
                std::this_thread::yield();
            }

            ConcurrentHashMapRetireList::ReclaimRetired();
        }

        void Drain() noexcept
        {
            while (ActiveReaders() != 0)
            {
                std::this_thread::yield();
            }
            ConcurrentHashMapRetireList::ReclaimRetired();
        }

        [[nodiscard]] auto ActiveReaders() const noexcept -> std::size_t
        {
            std::lock_guard<Sync::SpinLock> lock(m_recordsLock);
            std::size_t                     count = 0;
            for (const ReaderRecord* record = m_records; record; record = record->next)
                ++count;
            return count;
        }

        [[nodiscard]] auto PendingRetired() const noexcept -> std::size_t
        {
            return ConcurrentHashMapRetireList::PendingRetired();
        }

        [[nodiscard]] auto ReclaimedRetired() const noexcept -> std::size_t
        {
            return ConcurrentHashMapRetireList::ReclaimedRetired();
        }

    private:
        void Register(ReaderRecord& record) noexcept
        {
            record.epoch = m_epoch.load(std::memory_order_acquire);
            std::lock_guard<Sync::SpinLock> lock(m_recordsLock);
            record.next = m_records;
            m_records   = &record;
        }

        void Unregister(ReaderRecord& record) noexcept
        {
            std::lock_guard<Sync::SpinLock> lock(m_recordsLock);
            ReaderRecord**                  link = &m_records;
            while (*link && *link != &record)
                link = &(*link)->next;
            if (*link == &record)
                *link = record.next;
            record.next = nullptr;
        }

        [[nodiscard]] std::uint64_t MinimumActiveEpoch() const noexcept
        {
            std::lock_guard<Sync::SpinLock> lock(m_recordsLock);
            std::uint64_t                   minimum = m_epoch.load(std::memory_order_acquire);
            for (const ReaderRecord* record = m_records; record; record = record->next)
                minimum = std::min(minimum, record->epoch);
            return minimum;
        }

        mutable Sync::SpinLock     m_recordsLock {};
        ReaderRecord*              m_records {nullptr};
        std::atomic<std::uint64_t> m_epoch {1};
    };

    template<>
    class ConcurrentHashMapReclaimer<ReclamationPolicy::HazardPointers> : private ConcurrentHashMapRetireList
    {
        struct HazardRecord
        {
            std::atomic<void*> table {nullptr};
            std::atomic<void*> chain {nullptr};
            HazardRecord*      next {nullptr};
        };

    public:
        class ReadGuard
        {
        public:
            ReadGuard() = default;

            explicit ReadGuard(ConcurrentHashMapReclaimer* owner) noexcept
                : m_owner(owner)
            {
                if (m_owner)
                    m_owner->Register(m_record);
            }

            ReadGuard(const ReadGuard&)                    = delete;
            auto operator=(const ReadGuard&) -> ReadGuard& = delete;
            ReadGuard(ReadGuard&&)                         = delete;
            auto operator=(ReadGuard&&) -> ReadGuard&      = delete;

            ~ReadGuard()
            {
                if (m_owner)
                    m_owner->Unregister(m_record);
            }

        private:
            template<class T>
            [[nodiscard]] T* Protect(const std::atomic<T*>& pointer) noexcept
            {
                auto& hazard = m_protectCount++ == 0 ? m_record.table : m_record.chain;
                T*    candidate {nullptr};
                do
                {
                    candidate = pointer.load(std::memory_order_acquire);
                    hazard.store(candidate, std::memory_order_seq_cst);
                } while (candidate != pointer.load(std::memory_order_acquire));
                return candidate;
            }

            ConcurrentHashMapReclaimer* m_owner {nullptr};
            HazardRecord                m_record {};
            std::size_t                 m_protectCount {0};

            friend class ConcurrentHashMapReclaimer;
        };

        [[nodiscard]] auto Enter() const noexcept -> ReadGuard
        {
            return ReadGuard(const_cast<ConcurrentHashMapReclaimer*>(this));
        }

        template<class T>
        [[nodiscard]] auto Protect(const std::atomic<T*>& pointer, ReadGuard& guard) const noexcept -> T*
        {
            return guard.Protect(pointer);
        }

        template<class T>
        [[nodiscard]] auto Protect(const std::atomic<T*>& pointer, ReadGuard& guard) noexcept -> T*
        {
            return guard.Protect(pointer);
        }

        void Retire(void* object, void* context, void (*deleter)(void*, void*) noexcept)
        {
            ConcurrentHashMapRetireList::Retire(object, context, deleter);
        }

        void Poll() noexcept
        {
            ConcurrentHashMapRetireList::ReclaimIf(
                    [this](const ConcurrentHashMapRetiredRecord& record) noexcept {
                        return !IsHazard(record.object);
                    });
        }

        void Quiesce() noexcept
        {
            while (ActiveReaders() != 0)
                std::this_thread::yield();
            ConcurrentHashMapRetireList::ReclaimRetired();
        }

        void Drain() noexcept { Quiesce(); }

        [[nodiscard]] auto ActiveReaders() const noexcept -> std::size_t
        {
            std::lock_guard<Sync::SpinLock> lock(m_recordsLock);
            std::size_t                     count = 0;
            for (const HazardRecord* record = m_records; record; record = record->next)
                ++count;
            return count;
        }

        [[nodiscard]] auto PendingRetired() const noexcept -> std::size_t
        {
            return ConcurrentHashMapRetireList::PendingRetired();
        }

        [[nodiscard]] auto ReclaimedRetired() const noexcept -> std::size_t
        {
            return ConcurrentHashMapRetireList::ReclaimedRetired();
        }

    private:
        void Register(HazardRecord& record) noexcept
        {
            std::lock_guard<Sync::SpinLock> lock(m_recordsLock);
            record.next = m_records;
            m_records   = &record;
        }

        void Unregister(HazardRecord& record) noexcept
        {
            record.table.store(nullptr, std::memory_order_release);
            record.chain.store(nullptr, std::memory_order_release);
            std::lock_guard<Sync::SpinLock> lock(m_recordsLock);
            HazardRecord**                  link = &m_records;
            while (*link && *link != &record)
                link = &(*link)->next;
            if (*link == &record)
                *link = record.next;
            record.next = nullptr;
        }

        [[nodiscard]] bool IsHazard(const void* object) const noexcept
        {
            std::lock_guard<Sync::SpinLock> lock(m_recordsLock);
            for (const HazardRecord* record = m_records; record; record = record->next)
            {
                if (record->table.load(std::memory_order_acquire) == object ||
                    record->chain.load(std::memory_order_acquire) == object)
                    return true;
            }
            return false;
        }

        mutable Sync::SpinLock m_recordsLock {};
        HazardRecord*          m_records {nullptr};
    };

    template<>
    class ConcurrentHashMapReclaimer<ReclamationPolicy::ManualQuiesce> : private ConcurrentHashMapRetireList
    {
        static constexpr bool kTrackReadersInDebug =
#if defined(NGIN_DEBUG) || !defined(NDEBUG)
                true;
#else
                false;
#endif

    public:
        class ReadGuard
        {
        public:
            ReadGuard() = default;

            explicit ReadGuard(ConcurrentHashMapReclaimer* owner) noexcept
                : m_owner(owner)
            {
                if constexpr (kTrackReadersInDebug)
                {
                    if (m_owner)
                    {
                        m_owner->m_debugActiveReaders.fetch_add(1, std::memory_order_acquire);
                    }
                }
            }

            ReadGuard(const ReadGuard&)                    = delete;
            auto operator=(const ReadGuard&) -> ReadGuard& = delete;

            ReadGuard(ReadGuard&& other) noexcept
                : m_owner(other.m_owner)
            {
                other.m_owner = nullptr;
            }

            auto operator=(ReadGuard&& other) noexcept -> ReadGuard&
            {
                if (this != &other)
                {
                    Release();
                    m_owner       = other.m_owner;
                    other.m_owner = nullptr;
                }
                return *this;
            }

            ~ReadGuard()
            {
                Release();
            }

        private:
            void Release() noexcept
            {
                if constexpr (kTrackReadersInDebug)
                {
                    if (m_owner)
                    {
                        m_owner->m_debugActiveReaders.fetch_sub(1, std::memory_order_release);
                        m_owner = nullptr;
                    }
                }
            }

            ConcurrentHashMapReclaimer* m_owner {nullptr};
        };

        [[nodiscard]] auto Enter() const noexcept -> ReadGuard
        {
            return ReadGuard(const_cast<ConcurrentHashMapReclaimer*>(this));
        }

        template<class T>
        [[nodiscard]] auto Protect(const std::atomic<T*>& pointer, ReadGuard&) const noexcept -> T*
        {
            return pointer.load(std::memory_order_acquire);
        }

        template<class T>
        [[nodiscard]] auto Protect(const std::atomic<T*>& pointer, ReadGuard&) noexcept -> T*
        {
            return pointer.load(std::memory_order_acquire);
        }

        void Retire(void* object, void* context, void (*deleter)(void*, void*) noexcept)
        {
            ConcurrentHashMapRetireList::Retire(object, context, deleter);
        }

        void Poll() noexcept
        {
        }

        void Quiesce() noexcept
        {
            if constexpr (kTrackReadersInDebug)
            {
                NGIN_ASSERT(m_debugActiveReaders.load(std::memory_order_acquire) == 0 &&
                            "ManualQuiesce requires an external reader-free synchronization point");
            }
            ConcurrentHashMapRetireList::ReclaimRetired();
        }

        void Drain() noexcept
        {
            ConcurrentHashMapRetireList::ReclaimRetired();
        }

        [[nodiscard]] auto ActiveReaders() const noexcept -> std::size_t
        {
            if constexpr (kTrackReadersInDebug)
            {
                return m_debugActiveReaders.load(std::memory_order_acquire);
            }
            return 0;
        }

        [[nodiscard]] auto PendingRetired() const noexcept -> std::size_t
        {
            return ConcurrentHashMapRetireList::PendingRetired();
        }

        [[nodiscard]] auto ReclaimedRetired() const noexcept -> std::size_t
        {
            return ConcurrentHashMapRetireList::ReclaimedRetired();
        }

    private:
        mutable std::atomic<std::size_t> m_debugActiveReaders {0};
    };

    template<class Key, class Value>
    struct ConcurrentHashMapNode
    {
        std::size_t                 hash {0};
        ConcurrentHashMapNode*      next {nullptr};
        [[no_unique_address]] Key   key;
        [[no_unique_address]] Value value;

        template<class K, class V>
        ConcurrentHashMapNode(std::size_t h, ConcurrentHashMapNode* nextNode, K&& k, V&& v)
            : hash(h), next(nextNode), key(std::forward<K>(k)), value(std::forward<V>(v))
        {
        }
    };

    template<class Node>
    struct ConcurrentHashMapTable
    {
        using Bucket = std::atomic<Node*>;

        std::size_t bucketCount {0};
        Bucket*     buckets {nullptr};
    };

    [[nodiscard]] constexpr auto IsPowerOfTwoShardCount(const std::size_t value) noexcept -> bool
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    [[nodiscard]] constexpr auto CeilDivide(const std::size_t numerator, const std::size_t denominator) noexcept
            -> std::size_t
    {
        return denominator == 0 ? 0 : (numerator + denominator - 1) / denominator;
    }
}// namespace NGIN::Containers::detail

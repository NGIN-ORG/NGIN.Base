/// @file ConcurrentHashMapDetail.hpp
/// @brief Internal detail types for the concurrent hash map scaffold.
#pragma once

#include <NGIN/Defines.hpp>

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
    };

    class ConcurrentHashMapRetireList
    {
    protected:
        void Retire(void* object, void* context, void (*deleter)(void*, void*) noexcept)
        {
            if (!object || !deleter)
            {
                return;
            }

            auto* record = new ConcurrentHashMapRetiredRecord {
                    .object  = object,
                    .context = context,
                    .deleter = deleter,
                    .next    = m_retiredHead,
            };
            m_retiredHead = record;
        }

        void ReclaimRetired() noexcept
        {
            ConcurrentHashMapRetiredRecord* current = m_retiredHead;
            m_retiredHead                           = nullptr;

            while (current)
            {
                ConcurrentHashMapRetiredRecord* next = current->next;
                current->deleter(current->context, current->object);
                delete current;
                current = next;
            }
        }

    private:
        ConcurrentHashMapRetiredRecord* m_retiredHead {nullptr};
    };

    template<ReclamationPolicy Policy>
    class ConcurrentHashMapReclaimer : private ConcurrentHashMapRetireList
    {
    public:
        class ReadGuard
        {
        public:
            ReadGuard() = default;

            explicit ReadGuard(ConcurrentHashMapReclaimer* owner) noexcept
                : m_owner(owner)
            {
                if (m_owner)
                {
                    m_owner->m_activeReaders.fetch_add(1, std::memory_order_acquire);
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
                if (m_owner)
                {
                    m_owner->m_activeReaders.fetch_sub(1, std::memory_order_release);
                    m_owner = nullptr;
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
            if (m_activeReaders.load(std::memory_order_acquire) == 0)
            {
                ConcurrentHashMapRetireList::ReclaimRetired();
            }
        }

        void Quiesce() noexcept
        {
            while (m_activeReaders.load(std::memory_order_acquire) != 0)
            {
                std::this_thread::yield();
            }

            ConcurrentHashMapRetireList::ReclaimRetired();
        }

        void Drain() noexcept
        {
            while (m_activeReaders.load(std::memory_order_acquire) != 0)
            {
                std::this_thread::yield();
            }
            ConcurrentHashMapRetireList::ReclaimRetired();
        }

        [[nodiscard]] auto ActiveReaders() const noexcept -> std::size_t
        {
            return m_activeReaders.load(std::memory_order_acquire);
        }

    private:
        mutable std::atomic<std::size_t> m_activeReaders {0};
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

    [[nodiscard]] constexpr auto IsPowerOfTwo(const std::size_t value) noexcept -> bool
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    [[nodiscard]] constexpr auto CeilDivide(const std::size_t numerator, const std::size_t denominator) noexcept
            -> std::size_t
    {
        return denominator == 0 ? 0 : (numerator + denominator - 1) / denominator;
    }
}// namespace NGIN::Containers::detail

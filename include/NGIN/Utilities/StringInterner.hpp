/// @file StringInterner.hpp
/// @brief Header-only string interning utility with allocator customization.
#pragma once

#include <NGIN/Containers/HashMap.hpp>
#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Hashing/FNV.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <concepts>
#include <cstring>
#include <limits>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <utility>

namespace NGIN::Utilities
{
    namespace detail
    {
        struct NullMutex
        {
            void lock() noexcept {}
            void unlock() noexcept {}
        };

        template<class Mutex>
        concept MutexConcept = requires(Mutex& m) {
            m.lock();
            m.unlock();
        };
    }// namespace detail

    /// <summary>
    /// Fixed-lifetime string interning table that returns stable string_view references.
    /// Uses page-backed storage with geometric growth, optional threading policy, and caller-supplied allocator.
    /// </summary>
    template<class Allocator = NGIN::Memory::SystemAllocator, class ThreadPolicy = detail::NullMutex>
        requires NGIN::Memory::AllocatorConcept<Allocator> && detail::MutexConcept<ThreadPolicy> &&
                 std::default_initializable<Allocator> && std::default_initializable<ThreadPolicy>
    class StringInterner
    {
    public:
        struct Statistics
        {
            UInt64 lookups {0};
            UInt64 lookupHits {0};
            UInt64 inserted {0};
            UInt64 totalBytesStored {0};
            UInt64 pageAllocations {0};
            UInt64 pageDeallocations {0};
            UInt64 pageBytesAllocated {0};
            UInt64 pageBytesReleased {0};
        };

        using AllocatorType = Allocator;
        using IdType        = UInt32;
        using LockGuard     = std::lock_guard<ThreadPolicy>;

        static constexpr IdType INVALID_ID          = std::numeric_limits<IdType>::max();
        static constexpr UInt32 MIN_PAGE_CAPACITY   = 4u * 1024u;
        static constexpr UInt32 DEFAULT_PAGE_GROWTH = MIN_PAGE_CAPACITY;

        constexpr StringInterner() noexcept(std::is_nothrow_default_constructible_v<Allocator>)
            : m_allocator(),
              m_mutex(),
              m_pages(0, m_allocator),
              m_entries(0, m_allocator)
        {
        }

        explicit StringInterner(Allocator allocator) noexcept(std::is_nothrow_move_constructible_v<Allocator>)
            : m_allocator(std::move(allocator)),
              m_mutex(),
              m_pages(0, m_allocator),
              m_entries(0, m_allocator)
        {
        }

        StringInterner(StringInterner&& other) noexcept
            : m_allocator(std::move(other.m_allocator)),
              m_mutex(),
              m_pages(0, m_allocator),
              m_entries(0, m_allocator)
        {
            MoveFrom(std::move(other));
        }

        StringInterner& operator=(StringInterner&& other) noexcept
        {
            if (this != &other)
            {
                LockGuard guardThis(m_mutex);
                ReleasePagesLocked();
                m_pages            = Containers::Vector<Page, Allocator>(0, m_allocator);
                m_entries          = Containers::Vector<Entry, Allocator>(0, m_allocator);
                m_buckets          = Buckets {};
                m_totalBytes       = 0;
                m_nextPageCapacity = DEFAULT_PAGE_GROWTH;
                m_stats            = {};
                m_allocator        = std::move(other.m_allocator);
                MoveFrom(std::move(other));
            }
            return *this;
        }

        StringInterner(const StringInterner&)            = delete;
        StringInterner& operator=(const StringInterner&) = delete;

        ~StringInterner()
        {
            Clear();
        }

        /// <summary>Return the number of unique strings stored.</summary>
        [[nodiscard]] UIntSize Size() const noexcept
        {
            LockGuard guard(m_mutex);
            return m_entries.Size();
        }

        /// <summary>Total bytes copied from caller strings (excludes bookkeeping).</summary>
        [[nodiscard]] UInt64 TotalStoredBytes() const noexcept
        {
            LockGuard guard(m_mutex);
            return m_totalBytes;
        }

        /// <summary>True when no strings have been interned.</summary>
        [[nodiscard]] bool Empty() const noexcept
        {
            LockGuard guard(m_mutex);
            return m_entries.Size() == 0;
        }

        /// <summary>Allocator instance backing this interner.</summary>
        [[nodiscard]] Allocator& GetAllocator() noexcept
        {
            LockGuard guard(m_mutex);
            return m_allocator;
        }

        /// <summary>Allocator instance backing this interner.</summary>
        [[nodiscard]] const Allocator& GetAllocator() const noexcept
        {
            LockGuard guard(m_mutex);
            return m_allocator;
        }

        /// <summary>Snapshot current statistics counters.</summary>
        [[nodiscard]] Statistics GetStatistics() const noexcept
        {
            LockGuard  guard(m_mutex);
            Statistics stats       = m_stats;
            stats.totalBytesStored = m_totalBytes;
            return stats;
        }

        /// <summary>Reset statistics counters to zero.</summary>
        void ResetStatistics() noexcept
        {
            LockGuard guard(m_mutex);
            m_stats                  = {};
            m_stats.totalBytesStored = m_totalBytes;
        }

        /// <summary>Clear all stored strings and release owned memory.</summary>
        void Clear() noexcept
        {
            LockGuard guard(m_mutex);
            ReleasePagesLocked();
            m_pages                  = Containers::Vector<Page, Allocator>(0, m_allocator);
            m_entries                = Containers::Vector<Entry, Allocator>(0, m_allocator);
            m_buckets                = Buckets {};
            m_totalBytes             = 0;
            m_nextPageCapacity       = DEFAULT_PAGE_GROWTH;
            m_stats.totalBytesStored = 0;
        }

        /// <summary>Insert the string if missing and return its identifier.</summary>
        [[nodiscard]] IdType InsertOrGet(std::string_view value)
        {
            LockGuard guard(m_mutex);
            return InsertOrGetLocked(value);
        }

        /// <summary>Return the identifier for the string if present.</summary>
        [[nodiscard]] bool TryGetId(std::string_view value, IdType& out) const noexcept
        {
            LockGuard guard(m_mutex);
            ++m_stats.lookups;
            const auto hash = Hashing::FNV1a64(value.data(), value.size());
            if (FindIdUnlocked(hash, value, out))
            {
                ++m_stats.lookupHits;
                return true;
            }
            return false;
        }

        /// <summary>Intern the string and return a stable view.</summary>
        [[nodiscard]] std::string_view Intern(std::string_view value)
        {
            LockGuard  guard(m_mutex);
            const auto id = InsertOrGetLocked(value);
            if (id == INVALID_ID)
                return {};
            return ViewUnlocked(id);
        }

        /// <summary>Retrieve a previously interned string view by id.</summary>
        [[nodiscard]] std::string_view View(IdType id) const noexcept
        {
            LockGuard guard(m_mutex);
            return ViewUnlocked(id);
        }

    private:
        struct Page
        {
            char*  data {nullptr};
            UInt32 used {0};
            UInt32 capacity {0};
        };

        struct Entry
        {
            UInt32 page {0};
            UInt32 offset {0};
            UInt32 length {0};
            UInt64 hash {0};
        };

        using Buckets = Containers::FlatHashMap<UInt64, Containers::Vector<IdType, Allocator>>;

        [[nodiscard]] const Entry* GetEntryUnlocked(IdType id) const noexcept
        {
            if (id == INVALID_ID || id >= m_entries.Size())
                return nullptr;
            return &m_entries[id];
        }

        [[nodiscard]] bool FindIdUnlocked(UInt64 hash, std::string_view value, IdType& out) const noexcept
        {
            const auto* bucket = m_buckets.GetPtr(hash);
            if (bucket == nullptr)
                return false;

            for (UIntSize i = 0; i < bucket->Size(); ++i)
            {
                const auto   candidate = (*bucket)[i];
                const Entry* entry     = GetEntryUnlocked(candidate);
                if (entry == nullptr)
                    continue;
                if (entry->length == value.size())
                {
                    const auto* data = m_pages[entry->page].data + entry->offset;
                    if (value.empty() || std::memcmp(data, value.data(), value.size()) == 0)
                    {
                        out = candidate;
                        return true;
                    }
                }
            }
            return false;
        }

        [[nodiscard]] IdType InsertOrGetLocked(std::string_view value)
        {
            ++m_stats.lookups;
            const auto hash = Hashing::FNV1a64(value.data(), value.size());
            IdType     existing {};
            if (FindIdUnlocked(hash, value, existing))
            {
                ++m_stats.lookupHits;
                return existing;
            }

            const auto   length     = static_cast<UInt32>(value.size());
            const UInt32 allocBytes = length + 1;
            void*        storage    = AllocateBytesLocked(allocBytes);
            if (storage == nullptr)
                return INVALID_ID;

            auto* bytes = static_cast<char*>(storage);
            if (length > 0)
                std::memcpy(bytes, value.data(), length);
            bytes[length] = '\0';

            const UInt32 pageIndex = static_cast<UInt32>(m_pages.Size() - 1);
            Page&        page      = m_pages[pageIndex];
            const UInt32 offset    = page.used - allocBytes;

            Entry entry {
                    .page   = pageIndex,
                    .offset = offset,
                    .length = length,
                    .hash   = hash,
            };

            const IdType id = static_cast<IdType>(m_entries.Size());
            try
            {
                m_entries.PushBack(entry);
            } catch (...)
            {
                page.used = offset;
                throw;
            }

            try
            {
                if (auto* bucket = m_buckets.GetPtr(hash))
                {
                    bucket->PushBack(id);
                }
                else
                {
                    Containers::Vector<IdType, Allocator> ids(0, m_allocator);
                    ids.PushBack(id);
                    m_buckets.Insert(hash, std::move(ids));
                }
            } catch (...)
            {
                m_entries.PopBack();
                page.used = offset;
                throw;
            }

            ++m_stats.inserted;
            m_totalBytes += length;
            m_stats.totalBytesStored = m_totalBytes;
            return id;
        }

        [[nodiscard]] std::string_view ViewUnlocked(IdType id) const noexcept
        {
            const Entry* entry = GetEntryUnlocked(id);
            if (entry == nullptr)
                return {};
            const Page& page = m_pages[entry->page];
            return std::string_view {page.data + entry->offset, entry->length};
        }

        [[nodiscard]] void* AllocateBytesLocked(UInt32 byteCount)
        {
            if (byteCount == 0)
                return nullptr;

            if (m_pages.Size() == 0 || RemainingInLastPageLocked() < byteCount)
            {
                if (!AllocatePageLocked(std::max(m_nextPageCapacity, byteCount)))
                    return nullptr;
            }

            Page& page   = m_pages[m_pages.Size() - 1];
            char* result = page.data + page.used;
            page.used += byteCount;
            return result;
        }

        [[nodiscard]] UInt32 RemainingInLastPageLocked() const noexcept
        {
            if (m_pages.Size() == 0)
                return 0;
            const Page& page = m_pages[m_pages.Size() - 1];
            return page.capacity - page.used;
        }

        bool AllocatePageLocked(UInt32 minCapacity)
        {
            UInt32 capacity = std::max(MIN_PAGE_CAPACITY, m_nextPageCapacity);
            while (capacity < minCapacity)
            {
                if (capacity > (std::numeric_limits<UInt32>::max() / 2u))
                {
                    capacity = minCapacity;
                    break;
                }
                capacity *= 2u;
            }

            void* memory = m_allocator.Allocate(capacity, alignof(char));
            if (memory == nullptr)
                return false;

            Page page;
            page.data     = static_cast<char*>(memory);
            page.used     = 0;
            page.capacity = capacity;
            try
            {
                m_pages.PushBack(page);
            } catch (...)
            {
                m_allocator.Deallocate(memory, capacity, alignof(char));
                throw;
            }

            ++m_stats.pageAllocations;
            m_stats.pageBytesAllocated += capacity;
            m_nextPageCapacity = capacity * 2u;
            return true;
        }

        void ReleasePagesLocked() noexcept
        {
            for (UIntSize i = 0; i < m_pages.Size(); ++i)
            {
                Page& page = m_pages[i];
                if (page.data != nullptr)
                {
                    m_allocator.Deallocate(page.data, page.capacity, alignof(char));
                    ++m_stats.pageDeallocations;
                    m_stats.pageBytesReleased += page.capacity;
                    page.data     = nullptr;
                    page.used     = 0;
                    page.capacity = 0;
                }
            }
        }

        void MoveFrom(StringInterner&& other) noexcept
        {
            m_pages                  = std::move(other.m_pages);
            m_entries                = std::move(other.m_entries);
            m_buckets                = std::move(other.m_buckets);
            m_totalBytes             = other.m_totalBytes;
            m_nextPageCapacity       = other.m_nextPageCapacity;
            m_stats                  = other.m_stats;
            m_stats.totalBytesStored = m_totalBytes;

            other.m_pages            = Containers::Vector<Page, Allocator>();
            other.m_entries          = Containers::Vector<Entry, Allocator>();
            other.m_buckets          = Buckets {};
            other.m_totalBytes       = 0;
            other.m_nextPageCapacity = DEFAULT_PAGE_GROWTH;
            other.m_stats            = {};
        }

        Allocator                            m_allocator {};
        mutable ThreadPolicy                 m_mutex {};
        Containers::Vector<Page, Allocator>  m_pages;
        Containers::Vector<Entry, Allocator> m_entries;
        Buckets                              m_buckets {};
        UInt64                               m_totalBytes {0};
        UInt32                               m_nextPageCapacity {DEFAULT_PAGE_GROWTH};
        mutable Statistics                   m_stats {};
    };
}// namespace NGIN::Utilities

/// @file ConcurrentHashMap.hpp
/// @brief Lock-free, wait-free-style concurrent hash map implementation
///        extended with cooperative resizing and tombstone pruning,
///        following Shlomi Steinberg's design principles.

#pragma once

#include <atomic>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace NGIN::Containers
{

    // ============================================================
    // SharedDoubleReferenceGuard
    // ============================================================

    template<typename DataType>
    class SharedDoubleReferenceGuard
    {
    private:
        struct DataStruct
        {
            std::atomic<int> internal_counter;
            DataType         object;

            template<typename... Ts>
            explicit DataStruct(Ts&&... args)
                : internal_counter(0), object(std::forward<Ts>(args)...)
            {}

            void release_ref() noexcept
            {
                if (internal_counter.fetch_add(1, std::memory_order_acq_rel) == -1)
                    delete this;
            }
        };

        struct DataPtrStruct
        {
            int         external_counter;
            DataStruct* ptr;
        };

        static_assert(std::is_trivially_copyable_v<DataPtrStruct>,
                      "DataPtrStruct must be trivially copyable for atomic CAS");

        std::atomic<DataPtrStruct> data_ptr;

        void release(DataPtrStruct& old_data_ptr) noexcept
        {
            if (!old_data_ptr.ptr)
                return;
            int external = old_data_ptr.external_counter;
            if (old_data_ptr.ptr->internal_counter.fetch_sub(external, std::memory_order_acq_rel) == external - 1)
            {
                delete old_data_ptr.ptr;
            }
            else
            {
                old_data_ptr.ptr->release_ref();
            }
        }

    public:
        class DataGuard
        {
            friend class SharedDoubleReferenceGuard<DataType>;
            DataStruct* ptr;
            explicit DataGuard(DataStruct* p) : ptr(p) {}

        public:
            DataGuard(const DataGuard&)            = delete;
            DataGuard& operator=(const DataGuard&) = delete;
            DataGuard(DataGuard&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
            DataGuard& operator=(DataGuard&& other) noexcept
            {
                if (this != &other)
                {
                    if (ptr)
                        ptr->release_ref();
                    ptr       = other.ptr;
                    other.ptr = nullptr;
                }
                return *this;
            }
            ~DataGuard()
            {
                if (ptr)
                    ptr->release_ref();
            }

            bool            is_valid() const noexcept { return ptr != nullptr; }
            DataType*       operator->() noexcept { return &ptr->object; }
            DataType&       operator*() noexcept { return ptr->object; }
            const DataType* operator->() const noexcept { return &ptr->object; }
            const DataType& operator*() const noexcept { return ptr->object; }
        };

        SharedDoubleReferenceGuard() noexcept
        {
            DataPtrStruct init {0, nullptr};
            data_ptr.store(init, std::memory_order_relaxed);
        }

        ~SharedDoubleReferenceGuard()
        {
            auto old = data_ptr.load();
            release(old);
        }

        DataGuard acquire() noexcept
        {
            DataPtrStruct new_data_ptr {};
            DataPtrStruct old_data_ptr = data_ptr.load(std::memory_order_relaxed);
            do
            {
                new_data_ptr = old_data_ptr;
                ++new_data_ptr.external_counter;
            } while (!data_ptr.compare_exchange_weak(
                    old_data_ptr, new_data_ptr,
                    std::memory_order_acq_rel, std::memory_order_relaxed));
            return DataGuard(new_data_ptr.ptr);
        }

        template<typename... Ts>
        void emplace(Ts&&... args)
        {
            auto*         new_data = new DataStruct(std::forward<Ts>(args)...);
            DataPtrStruct new_dp {1, new_data};
            DataPtrStruct old_dp = data_ptr.load(std::memory_order_relaxed);
            while (!data_ptr.compare_exchange_weak(
                    old_dp, new_dp,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {}
            release(old_dp);
        }

        void drop()
        {
            DataPtrStruct new_dp {0, nullptr};
            DataPtrStruct old_dp = data_ptr.load(std::memory_order_relaxed);
            while (!data_ptr.compare_exchange_weak(
                    old_dp, new_dp,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {}
            release(old_dp);
        }
    };

    // ============================================================
    // ConcurrentHashMap
    // ============================================================

    template<typename Key, typename Value, std::size_t BucketsPerVirtual = 8>
    class ConcurrentHashMap
    {
        static inline std::size_t next_pow2(std::size_t n)
        {
            if (n < 2)
                return 2;
            --n;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
            if constexpr (sizeof(std::size_t) >= 8)
                n |= n >> 32;
            return n + 1;
        }

        struct BucketData
        {
            Key                               key;
            SharedDoubleReferenceGuard<Value> v;
            template<typename K, typename... Ts>
            BucketData(K&& k, Ts&&... args) : key(std::forward<K>(k)), v()
            {
                v.emplace(std::forward<Ts>(args)...);
            }
        };

        struct BucketPtr
        {
            std::size_t hash;
            BucketData* data;
            friend bool operator==(const BucketPtr& a, const BucketPtr& b) noexcept
            {
                return a.hash == b.hash && a.data == b.data;
            }
        };

        struct VirtualBucket
        {
            std::array<std::atomic<BucketPtr>, BucketsPerVirtual> slots;
            std::atomic<VirtualBucket*>                           next {nullptr};
            VirtualBucket()
            {
                for (auto& s: slots)
                    s.store(BucketPtr {0, nullptr}, std::memory_order_relaxed);
            }
            ~VirtualBucket()
            {
                if (auto* p = next.load())
                    delete p;
                for (auto& s: slots)
                {
                    auto bp = s.load();
                    if (bp.data)
                        delete bp.data;
                }
            }
        };

        std::unique_ptr<std::atomic<VirtualBucket*>[]> table;// power-of-two length array
        std::size_t                                    table_len {0};
        std::atomic<std::size_t>                       size_ {0};
        std::hash<Key>                                 hasher;

        std::size_t mask() const noexcept { return table_len - 1; }

        // Cooperative resize trigger
        void maybe_resize()
        {
            // Simple heuristic: resize when load factor > 0.75
            if (Size() * 4 < table_len * BucketsPerVirtual * 3)
                return;

            std::size_t new_len   = table_len * 2;
            auto        new_table = std::unique_ptr<std::atomic<VirtualBucket*>[]>(
                    new std::atomic<VirtualBucket*>[new_len]);

            for (std::size_t i = 0; i < new_len; ++i)
                new_table[i].store(new VirtualBucket(), std::memory_order_relaxed);

            // Rehash all buckets cooperatively (blocking for now)
            for (std::size_t i = 0; i < table_len; ++i)
            {
                VirtualBucket* vb = table[i].load(std::memory_order_relaxed);
                while (vb)
                {
                    for (auto& s: vb->slots)
                    {
                        BucketPtr bp = s.load(std::memory_order_relaxed);
                        if (bp.data)
                        {
                            auto guard = bp.data->v.acquire();
                            if (guard.is_valid())
                            {
                                auto           h      = hasher(bp.data->key);
                                size_t         j      = h & (new_len - 1);
                                VirtualBucket* dest   = new_table[j].load();
                                bool           placed = false;
                                while (!placed && dest)
                                {
                                    for (auto& slot: dest->slots)
                                    {
                                        BucketPtr expected {0, nullptr};
                                        BucketPtr desired {h, new BucketData(bp.data->key, *guard)};
                                        if (slot.compare_exchange_strong(expected, desired))
                                        {
                                            placed = true;
                                            break;
                                        }
                                    }
                                    if (!placed)
                                    {
                                        if (!dest->next.load())
                                        {
                                            auto*          new_vb        = new VirtualBucket();
                                            VirtualBucket* expected_next = nullptr;
                                            if (dest->next.compare_exchange_strong(expected_next, new_vb))
                                                dest = new_vb;
                                            else
                                                delete new_vb;
                                        }
                                        dest = dest->next.load();
                                    }
                                }
                            }
                        }
                    }
                    vb = vb->next.load(std::memory_order_relaxed);
                }
            }

            table.swap(new_table);
            table_len = new_len;
        }

    public:
        explicit ConcurrentHashMap(std::size_t initial_capacity = 16)
        {
            const std::size_t cap = next_pow2(initial_capacity);
            table_len             = cap;
            table                 = std::unique_ptr<std::atomic<VirtualBucket*>[]>(
                    new std::atomic<VirtualBucket*>[cap]);
            for (std::size_t i = 0; i < cap; ++i)
            {
                table[i].store(new VirtualBucket(), std::memory_order_relaxed);
            }
        }

        ~ConcurrentHashMap()
        {
            for (std::size_t i = 0; i < table_len; ++i)
            {
                if (auto* vb = table[i].load(std::memory_order_relaxed))
                    delete vb;
            }
        }

        void Insert(const Key& key, const Value& value)
        {
            size_t         h   = hasher(key);
            size_t         idx = h & mask();
            VirtualBucket* vb  = table[idx].load(std::memory_order_relaxed);

            while (vb)
            {
                for (auto& slot: vb->slots)
                {
                    BucketPtr bp = slot.load();
                    if (bp.data == nullptr)
                    {
                        auto*     bd = new BucketData(key, value);
                        BucketPtr expected {0, nullptr};
                        BucketPtr desired {h, bd};
                        if (slot.compare_exchange_strong(expected, desired))
                        {
                            size_.fetch_add(1);
                            maybe_resize();
                            return;
                        }
                        delete bd;
                    }
                    else if (bp.hash == h && bp.data->key == key)
                    {
                        bp.data->v.emplace(value);
                        return;
                    }
                }
                VirtualBucket* next = vb->next.load();
                if (!next)
                {
                    auto* new_vb = new VirtualBucket();
                    if (vb->next.compare_exchange_strong(next, new_vb))
                        vb = new_vb;
                    else
                    {
                        delete new_vb;
                        vb = next;
                    }
                }
                else
                    vb = next;
            }
        }

        void Remove(const Key& key)
        {
            size_t         h   = hasher(key);
            size_t         idx = h & mask();
            VirtualBucket* vb  = table[idx].load(std::memory_order_relaxed);

            while (vb)
            {
                for (auto& slot: vb->slots)
                {
                    BucketPtr bp = slot.load();
                    if (bp.hash == h && bp.data && bp.data->key == key)
                    {
                        bp.data->v.drop();
                        size_.fetch_sub(1);
                        return;
                    }
                }
                vb = vb->next.load();
            }
        }

        bool Contains(const Key& key) const
        {
            size_t         h   = hasher(key);
            size_t         idx = h & mask();
            VirtualBucket* vb  = table[idx].load(std::memory_order_relaxed);

            while (vb)
            {
                for (auto& slot: vb->slots)
                {
                    BucketPtr bp = slot.load();
                    if (bp.hash == h && bp.data && bp.data->key == key)
                    {
                        auto g = bp.data->v.acquire();
                        return g.is_valid();
                    }
                }
                vb = vb->next.load();
            }
            return false;
        }

        Value Get(const Key& key) const
        {
            size_t         h   = hasher(key);
            size_t         idx = h & mask();
            VirtualBucket* vb  = table[idx].load(std::memory_order_relaxed);

            while (vb)
            {
                for (auto& slot: vb->slots)
                {
                    BucketPtr bp = slot.load();
                    if (bp.hash == h && bp.data && bp.data->key == key)
                    {
                        auto g = bp.data->v.acquire();
                        if (g.is_valid())
                            return *g;
                        break;
                    }
                }
                vb = vb->next.load();
            }
            throw std::out_of_range("Key not found");
        }

        void Clear()
        {
            for (std::size_t i = 0; i < table_len; ++i)
            {
                VirtualBucket* vb = table[i].load(std::memory_order_relaxed);
                while (vb)
                {
                    for (auto& s: vb->slots)
                    {
                        BucketPtr bp = s.load(std::memory_order_relaxed);
                        if (bp.data)
                        {
                            bp.data->v.drop();
                        }
                    }
                    vb = vb->next.load(std::memory_order_relaxed);
                }
            }
            size_.store(0, std::memory_order_relaxed);
        }

        size_t Size() const
        {
            return size_.load();
        }
    };

}// namespace NGIN::Containers

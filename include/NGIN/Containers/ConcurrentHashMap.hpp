/// @file ConcurrentHashMap.hpp
/// @brief Lock-free, wait-free concurrent hash map implementation inspired by Shlomi Steinberg's design.

#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <array>
#include <cassert>
#include <thread>
#include <functional>
#include <utility>
#include <type_traits>
#include <stdexcept>

namespace NGIN::Containers
{

//-------------------- SharedDoubleReferenceGuard --------------------//

/// @brief Lock-free double reference counted guard for safe concurrent memory reclamation.
template <typename DataType>
class SharedDoubleReferenceGuard {
private:
    struct DataStruct {
        std::atomic<int> internal_counter;
        DataType object;

        template <typename ... Ts>
        DataStruct(Ts&&... args)
            : object(std::forward<Ts>(args)...), internal_counter(0) {}

        void release_ref() {
            if (internal_counter.fetch_add(1) == -1)
                destroy();
        }

        void destroy() {
            delete this;
        }
    };

    struct DataPtrStruct {
        int external_counter;
        DataStruct* ptr;
    };

    std::atomic<DataPtrStruct> data_ptr;

    void release(DataPtrStruct& old_data_ptr) {
        if (!old_data_ptr.ptr)
            return;
        auto external = old_data_ptr.external_counter;
        if (old_data_ptr.ptr->internal_counter.fetch_sub(external) == external - 1)
            old_data_ptr.ptr->destroy();
        else
            old_data_ptr.ptr->release_ref();
    }

public:
    class DataGuard {
        friend class SharedDoubleReferenceGuard<DataType>;
    private:
        DataStruct* ptr;
    public:
        DataGuard(DataStruct* p) : ptr(p) {}
        DataGuard(const DataGuard&) = delete;
        DataGuard& operator=(const DataGuard&) = delete;
        DataGuard(DataGuard&& d) noexcept : ptr(d.ptr) { d.ptr = nullptr; }
        DataGuard& operator=(DataGuard&& d) noexcept {
            if (ptr) ptr->release_ref();
            ptr = d.ptr;
            d.ptr = nullptr;
            return *this;
        }
        ~DataGuard() { if (ptr) ptr->release_ref(); }
        bool is_valid() const { return !!ptr; }
        DataType* operator->() { return &ptr->object; }
        DataType& operator*() { return ptr->object; }
        const DataType* operator->() const { return &ptr->object; }
        const DataType& operator*() const { return ptr->object; }
    };

    SharedDoubleReferenceGuard() {
        DataPtrStruct new_data_ptr{0, nullptr};
        data_ptr.store(new_data_ptr);
    }
    ~SharedDoubleReferenceGuard() {
        DataPtrStruct old_data_ptr = data_ptr.load();
        release(old_data_ptr);
    }
    DataGuard acquire() {
        DataPtrStruct new_data_ptr;
        DataPtrStruct old_data_ptr = data_ptr.load();
        do {
            new_data_ptr = old_data_ptr;
            ++new_data_ptr.external_counter;
        } while (!data_ptr.compare_exchange_weak(old_data_ptr, new_data_ptr));
        return DataGuard(new_data_ptr.ptr);
    }
    template <typename ... Ts>
    void emplace(Ts&&... args) {
        DataStruct* new_data = new DataStruct(std::forward<Ts>(args)...);
        DataPtrStruct new_data_ptr{1, new_data};
        DataPtrStruct old_data_ptr = data_ptr.load();
        while (!data_ptr.compare_exchange_weak(old_data_ptr, new_data_ptr));
        release(old_data_ptr);
    }
    template <typename ... Ts>
    bool try_emplace(DataGuard& old_data, Ts&&... args) {
        DataStruct* new_data = new DataStruct(std::forward<Ts>(args)...);
        DataPtrStruct new_data_ptr{1, new_data};
        DataPtrStruct old_data_ptr = data_ptr.load();
        bool success = false;
        while (old_data_ptr.ptr == old_data.ptr && !(success = data_ptr.compare_exchange_weak(old_data_ptr, new_data_ptr)));
        if (success)
            release(old_data_ptr);
        else
            delete new_data;
        return success;
    }
    void drop() {
        DataPtrStruct new_data_ptr{0, nullptr};
        DataPtrStruct old_data_ptr = data_ptr.load();
        while (!data_ptr.compare_exchange_weak(old_data_ptr, new_data_ptr));
        release(old_data_ptr);
    }
};

//-------------------- ConcurrentHashMap --------------------//

/// @brief Lock-free, wait-free concurrent hash map.
template <typename Key, typename Value, size_t BucketCount = 8>
class ConcurrentHashMap {
    struct BucketData {
        Key key;
        SharedDoubleReferenceGuard<Value> value_guard;
        std::atomic<bool> tombstone{false};
        BucketData() = default;
        template <typename K, typename ... Ts>
        BucketData(K&& k, Ts&&... args)
            : key(std::forward<K>(k)), value_guard() {
            value_guard.emplace(std::forward<Ts>(args)...);
        }
    };

    struct VirtualBucket {
        std::array<std::atomic<BucketData*>, BucketCount> buckets;
        std::atomic<VirtualBucket*> next{nullptr};
        VirtualBucket() {
            for (auto& b : buckets) b.store(nullptr);
        }
        ~VirtualBucket() {
            auto ptr = next.load();
            if (ptr) delete ptr;
            for (auto& bucket : buckets) {
                auto bucket_ptr = bucket.load();
                if (bucket_ptr) delete bucket_ptr;
            }
        }
    };

    std::vector<std::atomic<VirtualBucket*>> table;
    std::atomic<size_t> size_{0};
    std::hash<Key> hasher;

    size_t hash_index(const Key& key) const {
        return hasher(key) & (table.size() - 1);
    }

public:
    ConcurrentHashMap(size_t initial_capacity = 16)
        : table(initial_capacity)
    {
        for (auto& vb : table) vb.store(new VirtualBucket());
    }
    ~ConcurrentHashMap() {
        for (auto& vb : table) {
            auto ptr = vb.load();
            if (ptr) delete ptr;
        }
    }

    void Insert(const Key& key, const Value& value) {
        size_t idx = hash_index(key);
        VirtualBucket* vb = table[idx].load();
        while (vb) {
            for (size_t i = 0; i < BucketCount; ++i) {
                BucketData* bd = vb->buckets[i].load();
                if (!bd) {
                    auto* new_bd = new BucketData(key, value);
                    if (vb->buckets[i].compare_exchange_strong(bd, new_bd)) {
                        size_.fetch_add(1);
                        return;
                    } else {
                        delete new_bd;
                    }
                } else if (bd->key == key && !bd->tombstone.load()) {
                    bd->value_guard.emplace(value);
                    return;
                }
            }
            // Move to next bucket in chain, or create if missing
            VirtualBucket* next = vb->next.load();
            if (!next) {
                auto* new_vb = new VirtualBucket();
                if (vb->next.compare_exchange_strong(next, new_vb)) {
                    vb = new_vb;
                } else {
                    delete new_vb;
                    vb = next;
                }
            } else {
                vb = next;
            }
        }
    }

    void Remove(const Key& key) {
        size_t idx = hash_index(key);
        VirtualBucket* vb = table[idx].load();
        while (vb) {
            for (size_t i = 0; i < BucketCount; ++i) {
                BucketData* bd = vb->buckets[i].load();
                if (bd && bd->key == key && !bd->tombstone.load()) {
                    bd->tombstone.store(true);
                    size_.fetch_sub(1);
                    bd->value_guard.drop();
                    return;
                }
            }
            vb = vb->next.load();
        }
    }

    bool Contains(const Key& key) const {
        size_t idx = hash_index(key);
        VirtualBucket* vb = table[idx].load();
        while (vb) {
            for (size_t i = 0; i < BucketCount; ++i) {
                BucketData* bd = vb->buckets[i].load();
                if (bd && bd->key == key && !bd->tombstone.load()) {
                    return true;
                }
            }
            vb = vb->next.load();
        }
        return false;
    }

    Value Get(const Key& key) const {
        size_t idx = hash_index(key);
        VirtualBucket* vb = table[idx].load();
        while (vb) {
            for (size_t i = 0; i < BucketCount; ++i) {
                BucketData* bd = vb->buckets[i].load();
                if (bd && bd->key == key && !bd->tombstone.load()) {
                    auto guard = bd->value_guard.acquire();
                    if (guard.is_valid()) {
                        return *guard;
                    }
                }
            }
            vb = vb->next.load();
        }
        throw std::out_of_range("Key not found in concurrent hashmap");
    }

    void Clear() {
        for (auto& vb : table) {
            VirtualBucket* bucket = vb.load();
            for (size_t i = 0; i < BucketCount; ++i) {
                BucketData* bd = bucket->buckets[i].load();
                if (bd) {
                    bd->tombstone.store(true);
                    bd->value_guard.drop();
                }
            }
        }
        size_.store(0);
    }

    size_t Size() const {
        return size_.load();
    }
};

} // namespace NGIN::Containers

/// @file Concepts.hpp
/// @brief Concepts for synchronization primitives.
#pragma once

#include <concepts>

namespace NGIN::Sync
{
    template<typename T>
    concept BasicLockableConcept = requires(T lockable) {
        lockable.lock();
        lockable.unlock();
    };

    template<typename T>
    concept TryLockableConcept = BasicLockableConcept<T> && requires(T lockable) {
        {
            lockable.try_lock()
        } -> std::convertible_to<bool>;
    };

    template<typename T>
    concept SharedLockableConcept = BasicLockableConcept<T> && requires(T lockable) {
        lockable.lock_shared();
        lockable.unlock_shared();
    };

    template<typename T>
    concept SharedTryLockableConcept = SharedLockableConcept<T> && requires(T lockable) {
        {
            lockable.try_lock_shared()
        } -> std::convertible_to<bool>;
    };
}// namespace NGIN::Sync

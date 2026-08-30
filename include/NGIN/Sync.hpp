#pragma once

/// @file Sync.hpp
/// @brief Umbrella include for synchronization primitives and lock helpers.

#include <NGIN/Sync/AtomicCondition.hpp>
#include <NGIN/Sync/Concepts.hpp>
#include <NGIN/Sync/LockGuard.hpp>
#include <NGIN/Sync/Mutex.hpp>
#include <NGIN/Sync/ReadWriteLock.hpp>
#include <NGIN/Sync/RecursiveMutex.hpp>
#include <NGIN/Sync/Semaphore.hpp>
#include <NGIN/Sync/SharedMutex.hpp>
#include <NGIN/Sync/SpinLock.hpp>
#include <NGIN/Sync/TicketLock.hpp>

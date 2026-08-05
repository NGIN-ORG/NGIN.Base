/// @file Primitives.cpp
/// @brief Behavioral tests for NGIN synchronization primitives and guards.

#include <NGIN/Sync/Concepts.hpp>
#include <NGIN/Sync/FIFOSpinLock.hpp>
#include <NGIN/Sync/LockGuard.hpp>
#include <NGIN/Sync/Mutex.hpp>
#include <NGIN/Sync/ReadWriteLock.hpp>
#include <NGIN/Sync/RecursiveMutex.hpp>
#include <NGIN/Sync/Semaphore.hpp>
#include <NGIN/Sync/SharedMutex.hpp>
#include <NGIN/Sync/SpinLock.hpp>

#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <utility>
#include <vector>

namespace NGIN::Sync
{
    namespace
    {
        template<class TLock>
        void VerifyExclusiveTryLockContract()
        {
            TLock lock;
            lock.Lock();

            bool        acquiredWhileHeld = true;
            std::thread contender([&] {
                acquiredWhileHeld = lock.TryLock();
                if (acquiredWhileHeld)
                {
                    lock.Unlock();
                }
            });
            contender.join();

            CHECK_FALSE(acquiredWhileHeld);
            lock.Unlock();

            REQUIRE(lock.TryLock());
            lock.Unlock();
        }

        template<class TLock>
        void VerifyContendedCounter()
        {
            constexpr int threadCount         = 4;
            constexpr int incrementsPerThread = 1'000;

            TLock                    lock;
            int                      value = 0;
            std::vector<std::thread> workers;
            workers.reserve(threadCount);

            for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
            {
                workers.emplace_back([&] {
                    for (int increment = 0; increment < incrementsPerThread; ++increment)
                    {
                        LockGuard guard {lock};
                        ++value;
                    }
                });
            }

            for (auto& worker: workers)
            {
                worker.join();
            }

            CHECK(value == threadCount * incrementsPerThread);
        }

        struct CountingLock
        {
            void lock() noexcept
            {
                ++lockCalls;
                locked = true;
            }

            void unlock() noexcept
            {
                ++unlockCalls;
                locked = false;
            }

            int  lockCalls {0};
            int  unlockCalls {0};
            bool locked {false};
        };

        struct CountingSharedLock
        {
            void lock() noexcept {}
            void unlock() noexcept {}

            void lock_shared() noexcept
            {
                ++lockCalls;
                locked = true;
            }

            void unlock_shared() noexcept
            {
                ++unlockCalls;
                locked = false;
            }

            int  lockCalls {0};
            int  unlockCalls {0};
            bool locked {false};
        };
    }// namespace

    static_assert(BasicLockableConcept<Mutex>);
    static_assert(TryLockableConcept<Mutex>);
    static_assert(SharedLockableConcept<SharedMutex>);
    static_assert(SharedTryLockableConcept<SharedMutex>);

    TEST_CASE("Exclusive synchronization primitives report try-lock failure", "[Sync][Primitives]")
    {
        SECTION("Mutex")
        {
            VerifyExclusiveTryLockContract<Mutex>();
        }
        SECTION("RecursiveMutex")
        {
            VerifyExclusiveTryLockContract<RecursiveMutex>();
        }
        SECTION("SpinLock")
        {
            VerifyExclusiveTryLockContract<SpinLock>();
        }
        SECTION("TicketLock")
        {
            VerifyExclusiveTryLockContract<TicketLock>();
        }
    }

    TEST_CASE("RecursiveMutex permits recursive acquisition", "[Sync][RecursiveMutex]")
    {
        RecursiveMutex mutex;
        mutex.Lock();
        REQUIRE(mutex.TryLock());
        mutex.Unlock();
        mutex.Unlock();
    }

    TEST_CASE("SharedMutex distinguishes shared and exclusive acquisition", "[Sync][SharedMutex]")
    {
        SharedMutex mutex;
        mutex.LockShared();

        bool        acquiredShared = false;
        std::thread reader([&] {
            acquiredShared = mutex.TryLockShared();
            if (acquiredShared)
            {
                mutex.UnlockShared();
            }
        });
        reader.join();

        bool        acquiredExclusive = true;
        std::thread writer([&] {
            acquiredExclusive = mutex.TryLock();
            if (acquiredExclusive)
            {
                mutex.Unlock();
            }
        });
        writer.join();

        CHECK(acquiredShared);
        CHECK_FALSE(acquiredExclusive);
        mutex.UnlockShared();

        mutex.Lock();
        bool        acquiredWhileExclusive = true;
        std::thread blockedReader([&] {
            acquiredWhileExclusive = mutex.TryLockShared();
            if (acquiredWhileExclusive)
            {
                mutex.UnlockShared();
            }
        });
        blockedReader.join();
        CHECK_FALSE(acquiredWhileExclusive);
        mutex.Unlock();
    }

    TEST_CASE("ReadWriteLock try methods return acquisition state", "[Sync][ReadWriteLock]")
    {
        ReadWriteLock lock;
        lock.StartRead();

        bool        readAcquired = false;
        std::thread reader([&] {
            readAcquired = lock.TryStartRead();
            if (readAcquired)
            {
                lock.EndRead();
            }
        });
        reader.join();

        bool        writeAcquired = true;
        std::thread writer([&] {
            writeAcquired = lock.TryStartWrite();
            if (writeAcquired)
            {
                lock.EndWrite();
            }
        });
        writer.join();

        CHECK(readAcquired);
        CHECK_FALSE(writeAcquired);
        lock.EndRead();

        REQUIRE(lock.TryStartWrite());
        lock.EndWrite();
    }

    TEST_CASE("Semaphore exposes permit availability through try-lock", "[Sync][Semaphore]")
    {
        Semaphore<1> semaphore {1};
        semaphore.Lock();

        bool        acquired = true;
        std::thread contender([&] {
            acquired = semaphore.TryLock();
            if (acquired)
            {
                semaphore.Unlock();
            }
        });
        contender.join();

        CHECK_FALSE(acquired);
        semaphore.Unlock();
        REQUIRE(semaphore.TryLock());
        semaphore.Unlock();
    }

    TEST_CASE("LockGuard move transfers sole unlock responsibility", "[Sync][LockGuard]")
    {
        CountingLock lock;
        {
            LockGuard first {lock};
            CHECK(lock.locked);
            LockGuard second {std::move(first)};
            CHECK(lock.locked);
        }

        CHECK(lock.lockCalls == 1);
        CHECK(lock.unlockCalls == 1);
        CHECK_FALSE(lock.locked);
    }

    TEST_CASE("SharedLockGuard move transfers sole unlock responsibility", "[Sync][LockGuard]")
    {
        CountingSharedLock lock;
        {
            SharedLockGuard first {lock};
            CHECK(lock.locked);
            SharedLockGuard second {std::move(first)};
            CHECK(lock.locked);
        }

        CHECK(lock.lockCalls == 1);
        CHECK(lock.unlockCalls == 1);
        CHECK_FALSE(lock.locked);
    }

    TEST_CASE("Synchronization primitives protect contended state", "[Sync][Contention]")
    {
        SECTION("Mutex")
        {
            VerifyContendedCounter<Mutex>();
        }
        SECTION("SpinLock")
        {
            VerifyContendedCounter<SpinLock>();
        }
        SECTION("TicketLock")
        {
            VerifyContendedCounter<TicketLock>();
        }
    }
}// namespace NGIN::Sync

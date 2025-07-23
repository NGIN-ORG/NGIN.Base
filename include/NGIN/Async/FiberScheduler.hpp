/// <summary>
/// Fiber/thread hybrid scheduler for NGIN::Async::IScheduler (Windows-only, cooperative).
/// </summary>
#pragma once

#include <queue>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <coroutine>
#include <chrono>
#include <cassert>
#include <functional>
#include "IScheduler.hpp"
#include "Fiber.hpp"

namespace NGIN::Async
{
    class FiberScheduler : public IScheduler
    {
    private:
        constexpr static UIntSize DEFAULT_NUM_FIBERS  = 128;
        constexpr static UIntSize DEFAULT_NUM_THREADS = 4;

    public:
        using clock      = std::chrono::steady_clock;
        using time_point = clock::time_point;

        FiberScheduler(size_t numThreads = DEFAULT_NUM_THREADS, size_t numFibers = DEFAULT_NUM_FIBERS)
            : m_stop(false)
        {
            // Preallocate fibers
            m_allFibers.reserve(numFibers);
            for (size_t i = 0; i < numFibers; ++i)
            {
                auto fiber = std::make_unique<Fiber>();
                m_fiberPool.push(fiber.get());
                m_allFibers.push_back(std::move(fiber));
            }

            // Launch worker threads
            for (size_t i = 0; i < numThreads; ++i)
                m_threads.emplace_back([this] { WorkerLoop(); });

            // Launch driver thread
            m_driverThread = std::thread([this] { DriverLoop(); });
        }

        ~FiberScheduler() override
        {
            m_stop = true;
            m_readyCv.notify_all();

            if (m_driverThread.joinable())
                m_driverThread.join();

            for (auto& t: m_threads)
                if (t.joinable())
                    t.join();

            // Optionally: clear sleeping tasks
            {
                std::lock_guard lock(m_timersMutex);
                while (!m_sleepingTasks.empty())
                    m_sleepingTasks.pop();
            }
        }

        void Schedule(std::coroutine_handle<> coro) noexcept override
        {
            {
                std::lock_guard lock(m_readyMutex);
                m_readyQueue.push(coro);
            }
            m_readyCv.notify_one();
        }

        void ScheduleDelay(std::coroutine_handle<> coro, std::chrono::steady_clock::time_point resumeAt) override
        {
            {
                std::lock_guard lock(m_timersMutex);
                m_sleepingTasks.emplace(resumeAt, coro);
            }
            // Wake up the driver thread to possibly update sleep deadline
        }

        bool RunOne() override
        {
            // Not needed: scheduler runs automatically.
            return false;
        }

        void RunUntilIdle() noexcept override
        {
            // Not needed: scheduler runs automatically.
        }

        void CancelAll() noexcept override
        {
            {
                std::lock_guard lock(m_readyMutex);
                std::queue<std::coroutine_handle<>> empty;
                std::swap(m_readyQueue, empty);
            }
            {
                std::lock_guard lock(m_timersMutex);
                while (!m_sleepingTasks.empty())
                    m_sleepingTasks.pop();
            }
        }

        void SetPriority(int priority) noexcept override
        {
            m_priority = priority;
        }
        void SetAffinity(uint64_t affinityMask) noexcept override
        {
            m_affinityMask = affinityMask;
        }
        void OnTaskStart(uint64_t, const char*) noexcept override {}
        void OnTaskSuspend(uint64_t) noexcept override {}
        void OnTaskResume(uint64_t) noexcept override {}
        void OnTaskComplete(uint64_t) noexcept override {}

    private:
        std::atomic<bool> m_stop {false};
        int m_priority {0};
        uint64_t m_affinityMask {0};

        // Fibers
        std::vector<std::unique_ptr<Fiber>> m_allFibers;
        std::queue<Fiber*> m_fiberPool;
        std::mutex m_fiberMutex;

        // Worker threads
        std::vector<std::thread> m_threads;
        // Driver thread (timer pump)
        std::thread m_driverThread;

        // Ready queue
        std::queue<std::coroutine_handle<>> m_readyQueue;
        std::mutex m_readyMutex;
        std::condition_variable m_readyCv;

        // Delayed tasks
        using SleepEntry = std::pair<time_point, std::coroutine_handle<>>;
        struct SleepEntryCompare
        {
            bool operator()(const SleepEntry& a, const SleepEntry& b) const
            {
                return a.first > b.first;// Min-heap: soonest first
            }
        };
        std::priority_queue<SleepEntry, std::vector<SleepEntry>, SleepEntryCompare> m_sleepingTasks;
        std::mutex m_timersMutex;

        // Called from driver thread: check for timers that need to be resumed
        void CheckSleepingTasks()
        {
            auto now = clock::now();
            std::lock_guard lock(m_timersMutex);
            while (!m_sleepingTasks.empty() && m_sleepingTasks.top().first <= now)
            {
                // Move expired task to ready queue
                Schedule(m_sleepingTasks.top().second);
                m_sleepingTasks.pop();
            }
        }

        // Driver thread: manages timers/delays
        void DriverLoop()
        {
            while (!m_stop)
            {
                std::chrono::milliseconds nextSleep = std::chrono::milliseconds(100);

                {
                    std::lock_guard lock(m_timersMutex);
                    if (!m_sleepingTasks.empty())
                    {
                        auto now      = clock::now();
                        auto resumeAt = m_sleepingTasks.top().first;
                        if (resumeAt > now)
                            nextSleep = std::chrono::duration_cast<std::chrono::milliseconds>(resumeAt - now);
                        else
                            nextSleep = std::chrono::milliseconds(0);
                    }
                }

                // Check for sleeping tasks to wake up
                CheckSleepingTasks();
                m_readyCv.notify_all();

                // Sleep for next deadline or default 100ms
                std::this_thread::sleep_for(nextSleep);
            }
        }

        // Worker threads: each pulls a ready coroutine, runs it on a fiber, returns fiber to pool
        void WorkerLoop()
        {
            // Convert this thread to a fiber (once)
            if (!Fiber::IsMainFiberInitialized())
                Fiber::EnsureMainFiber();

            while (!m_stop)
            {
                std::coroutine_handle<> coro = nullptr;
                {
                    std::unique_lock lock(m_readyMutex);
                    m_readyCv.wait(lock, [this] {
                        return m_stop || !m_readyQueue.empty();
                    });
                    if (m_stop)
                        break;
                    if (!m_readyQueue.empty())
                    {
                        coro = m_readyQueue.front();
                        m_readyQueue.pop();
                    }
                }

                if (!coro)
                    continue;

                Fiber* fiber = nullptr;
                {
                    std::lock_guard lock(m_fiberMutex);
                    if (m_fiberPool.empty())
                    {
                        // No fiber available, put back and yield
                        std::lock_guard lock2(m_readyMutex);
                        m_readyQueue.push(coro);
                        std::this_thread::yield();
                        continue;
                    }
                    fiber = m_fiberPool.front();
                    m_fiberPool.pop();
                }

                fiber->assign(NGIN::Utilities::Callable<void()>([coro]() { coro.resume(); }));
                fiber->Resume();

                {
                    std::lock_guard lock(m_fiberMutex);
                    m_fiberPool.push(fiber);
                }
            }
        }

        FiberScheduler(const FiberScheduler&)            = delete;
        FiberScheduler& operator=(const FiberScheduler&) = delete;
    };
}// namespace NGIN::Async

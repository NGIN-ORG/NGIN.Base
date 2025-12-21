/// <summary>
/// Thread-pool-based scheduler implementation for NGIN::Execution::IScheduler (header-only).
/// </summary>
#pragma once

#include "IScheduler.hpp"
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

namespace NGIN::Execution
{
    /// <summary>
    /// Scheduler that dispatches coroutines onto a pool of worker threads.
    /// </summary>
    class ThreadPoolScheduler : public IScheduler
    {
    public:
        /// <summary>
        /// Construct a thread pool with the given number of threads.
        /// </summary>
        explicit ThreadPoolScheduler(size_t threadCount = std::thread::hardware_concurrency())
            : m_stop(false)
        {
            for (size_t i = 0; i < threadCount; ++i)
            {
                m_threads.emplace_back([this] { WorkerLoop(); });
            }
        }

        /// <summary>
        /// Destructor - stops all workers and cleans up.
        /// </summary>
        ~ThreadPoolScheduler() override
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stop = true;
            }
            m_cv.notify_all();
            for (auto& t: m_threads)
            {
                if (t.joinable())
                    t.join();
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                while (!m_queue.empty())
                    m_queue.pop();
            }
        }

        void Schedule(std::coroutine_handle<> coro) noexcept override
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.push(coro);
            }
            m_cv.notify_one();
        }

        void ScheduleDelay(std::coroutine_handle<> coro, std::chrono::steady_clock::time_point resumeAt) override
        {
            auto now = std::chrono::steady_clock::now();
            auto dur = (resumeAt > now) ? (resumeAt - now) : std::chrono::milliseconds(0);
            std::thread([this, coro, dur]() {
                std::this_thread::sleep_for(dur);
                Schedule(coro);
            }).detach();
        }


        bool RunOne() noexcept override
        {
            std::coroutine_handle<> work;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_queue.empty())
                    return false;
                work = m_queue.front();
                m_queue.pop();
            }
            if (work)
                work.resume();
            return true;
        }

        void RunUntilIdle() noexcept override
        {
            while (RunOne()) {}
        }

        void CancelAll() noexcept override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (!m_queue.empty())
                m_queue.pop();
        }

        void SetPriority(int priority) noexcept override
        {
            m_priority = priority;
        }

        void SetAffinity(uint64_t affinityMask) noexcept override
        {
            m_affinityMask = affinityMask;
            // Optionally: apply affinity to worker threads
        }

        void OnTaskStart(uint64_t, const char*) noexcept override {}
        void OnTaskSuspend(uint64_t) noexcept override {}
        void OnTaskResume(uint64_t) noexcept override {}
        void OnTaskComplete(uint64_t) noexcept override {}


    private:
        void WorkerLoop() noexcept
        {
            while (true)
            {
                std::coroutine_handle<> coro = nullptr;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
                    if (m_stop && m_queue.empty())
                        return;
                    coro = m_queue.front();
                    m_queue.pop();
                }
                if (coro && !coro.done())
                    coro.resume();
            }
        }

        std::vector<std::thread> m_threads;
        std::queue<std::coroutine_handle<>> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic<bool> m_stop;
        int m_priority {0};
        uint64_t m_affinityMask {0};
    };

}// namespace NGIN::Execution

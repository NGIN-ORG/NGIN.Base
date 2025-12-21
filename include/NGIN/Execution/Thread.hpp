/// @file Thread.hpp
/// @brief Cross-platform thread wrapper.
#pragma once

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace NGIN::Execution
{
    class Thread
    {
    public:
        Thread() noexcept = default;

        explicit Thread(std::function<void()> func)
            : m_thread(std::move(func))
        {
        }

        ~Thread()
        {
            if (m_thread.joinable())
            {
                std::terminate();
            }
        }

        Thread(const Thread&)            = delete;
        Thread& operator=(const Thread&) = delete;

        Thread(Thread&& other) noexcept
            : m_thread(std::move(other.m_thread))
        {
        }

        Thread& operator=(Thread&& other) noexcept
        {
            if (this != &other)
            {
                m_thread = std::move(other.m_thread);
            }
            return *this;
        }

        void Start(std::function<void()> func)
        {
            if (m_thread.joinable())
            {
                throw std::logic_error("Thread already started");
            }
            m_thread = std::thread(std::move(func));
        }

        void Join()
        {
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }

        void Detach()
        {
            if (m_thread.joinable())
            {
                m_thread.detach();
            }
        }

        [[nodiscard]] bool IsJoinable() const noexcept
        {
            return m_thread.joinable();
        }

        void SetName(const std::string& name)
        {
#if defined(_WIN32)
            if (!m_thread.joinable())
            {
                return;
            }
            auto h = m_thread.native_handle();
            std::wstring wname(name.begin(), name.end());
            ::SetThreadDescription(h, wname.c_str());
#else
            (void) name;
#endif
        }

        [[nodiscard]] std::thread::id GetId() const noexcept
        {
            return m_thread.get_id();
        }

        template<typename Rep, typename Period>
        static void SleepFor(const std::chrono::duration<Rep, Period>& d)
        {
            std::this_thread::sleep_for(d);
        }

        template<typename Clock, typename Duration>
        static void SleepUntil(const std::chrono::time_point<Clock, Duration>& tp)
        {
            std::this_thread::sleep_until(tp);
        }

    private:
        std::thread m_thread;
    };
}// namespace NGIN::Execution

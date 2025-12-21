/// @file Thread.hpp
/// @brief Cross-platform thread wrapper.
#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Units.hpp>

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

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        static void SleepFor(const TUnit& duration) noexcept
        {
            NGIN::Time::SleepFor(duration);
        }

        static void SleepUntil(NGIN::Time::TimePoint timePoint) noexcept
        {
            const auto now = NGIN::Time::MonotonicClock::Now();
            if (timePoint <= now)
            {
                return;
            }
            const auto deltaNs = timePoint.ToNanoseconds() - now.ToNanoseconds();
            NGIN::Time::SleepFor(NGIN::Units::Nanoseconds(static_cast<double>(deltaNs)));
        }

    private:
        std::thread m_thread;
    };
}// namespace NGIN::Execution

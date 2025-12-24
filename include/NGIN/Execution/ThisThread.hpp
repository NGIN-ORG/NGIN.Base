/// @file ThisThread.hpp
/// @brief Calling-thread utilities (NGIN replacement for std::this_thread).
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/Sleep.hpp>
#include <NGIN/Time/TimePoint.hpp>
#include <NGIN/Units.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#if defined(_WIN32)
extern "C"
{
    __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
    __declspec(dllimport) void* __stdcall GetCurrentThread();
    __declspec(dllimport) long __stdcall SetThreadDescription(void* hThread, const wchar_t* lpThreadDescription);
    __declspec(dllimport) int __stdcall MultiByteToWideChar(
            unsigned int codePage,
            unsigned long dwFlags,
            const char* lpMultiByteStr,
            int cbMultiByte,
            wchar_t* lpWideCharStr,
            int cchWideChar);
    __declspec(dllimport) int __stdcall SwitchToThread();
}
#elif defined(__linux__)
#include <sched.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sched.h>
#include <pthread.h>
#else
#include <sched.h>
#include <pthread.h>
#include <functional>
#endif

namespace NGIN::Execution::ThisThread
{
    using ThreadId = NGIN::UInt64;

    [[nodiscard]] inline ThreadId GetId() noexcept
    {
#if defined(_WIN32)
        return static_cast<ThreadId>(::GetCurrentThreadId());
#elif defined(__linux__)
        return static_cast<ThreadId>(::syscall(SYS_gettid));
#elif defined(__APPLE__)
        std::uint64_t tid = 0;
        (void) ::pthread_threadid_np(nullptr, &tid);
        return static_cast<ThreadId>(tid);
#else
        return static_cast<ThreadId>(std::hash<pthread_t> {}(::pthread_self()));
#endif
    }

    inline void YieldNow() noexcept
    {
#if defined(_WIN32)
        (void) ::SwitchToThread();
#else
        (void) ::sched_yield();
#endif
    }

    inline void RelaxCpu() noexcept
    {
        NGIN_CPU_RELAX();
    }

    template<typename TUnit>
        requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
    inline void SleepFor(const TUnit& duration) noexcept
    {
        NGIN::Time::SleepFor(duration);
    }

    inline void SleepUntil(NGIN::Time::TimePoint timePoint) noexcept
    {
        const auto now = NGIN::Time::MonotonicClock::Now();
        if (timePoint <= now)
        {
            return;
        }
        const auto deltaNs = timePoint.ToNanoseconds() - now.ToNanoseconds();
        NGIN::Time::SleepFor(NGIN::Units::Nanoseconds(static_cast<double>(deltaNs)));
    }

    [[nodiscard]] inline bool SetName(std::string_view name) noexcept
    {
        if (name.empty())
        {
            return false;
        }

#if defined(_WIN32)
        constexpr unsigned int  CP_UTF8 = 65001u;
        constexpr unsigned long Flags   = 0ul;
        std::array<wchar_t, 64> wide {};
        const auto              srcLen = static_cast<int>(std::min<std::size_t>(name.size(), 63));

        const int written =
                ::MultiByteToWideChar(CP_UTF8, Flags, name.data(), srcLen, wide.data(), static_cast<int>(wide.size() - 1));
        if (written <= 0)
        {
            return false;
        }
        wide[static_cast<std::size_t>(written)] = L'\0';

        const auto hr = ::SetThreadDescription(::GetCurrentThread(), wide.data());
        return hr >= 0;

#elif defined(__linux__)
        std::array<char, 16> truncated {};
        const auto           len = std::min<std::size_t>(name.size(), truncated.size() - 1);
        for (std::size_t i = 0; i < len; ++i)
        {
            truncated[i] = name[i];
        }
        truncated[len] = '\0';
        return ::pthread_setname_np(::pthread_self(), truncated.data()) == 0;

#elif defined(__APPLE__)
        std::array<char, 64> truncated {};
        const auto           len = std::min<std::size_t>(name.size(), truncated.size() - 1);
        for (std::size_t i = 0; i < len; ++i)
        {
            truncated[i] = name[i];
        }
        truncated[len] = '\0';
        return ::pthread_setname_np(truncated.data()) == 0;

#else
        (void) name;
        return false;
#endif
    }
}// namespace NGIN::Execution::ThisThread

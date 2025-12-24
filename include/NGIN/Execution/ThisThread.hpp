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
    __declspec(dllimport) unsigned long __stdcall GetActiveProcessorCount(unsigned short groupNumber);
    __declspec(dllimport) std::uintptr_t __stdcall SetThreadAffinityMask(void* hThread, std::uintptr_t dwThreadAffinityMask);
    __declspec(dllimport) int __stdcall SetThreadPriority(void* hThread, int nPriority);
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
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#else
#include <sched.h>
#include <pthread.h>
#include <functional>
#include <unistd.h>
#endif

namespace NGIN::Execution::ThisThread
{
    using ThreadId = NGIN::UInt64;

    [[nodiscard]] inline std::uint32_t HardwareConcurrency() noexcept
    {
#if defined(_WIN32)
        constexpr unsigned short ALL_PROCESSOR_GROUPS = 0xFFFFu;
        const auto              count                = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        return count == 0 ? 1u : static_cast<std::uint32_t>(count);
#elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        const auto v = ::sysconf(_SC_NPROCESSORS_ONLN);
        if (v <= 0)
        {
            return 1u;
        }
        return static_cast<std::uint32_t>(v);
#else
        return 1u;
#endif
    }

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

    [[nodiscard]] inline bool SetAffinity(UInt64 affinityMask) noexcept
    {
        if (affinityMask == 0)
        {
            return false;
        }

#if defined(_WIN32)
        return ::SetThreadAffinityMask(::GetCurrentThread(), static_cast<std::uintptr_t>(affinityMask)) != 0;
#elif defined(__linux__)
        cpu_set_t set {};
        CPU_ZERO(&set);
        for (int bit = 0; bit < static_cast<int>(sizeof(UInt64) * 8); ++bit)
        {
            if ((affinityMask & (1ull << static_cast<UInt64>(bit))) != 0)
            {
                CPU_SET(bit, &set);
            }
        }
        return ::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set) == 0;
#else
        (void) affinityMask;
        return false;
#endif
    }

    [[nodiscard]] inline bool SetPriority(int value) noexcept
    {
#if defined(_WIN32)
        return ::SetThreadPriority(::GetCurrentThread(), value) != 0;
#elif defined(__linux__)
        // Best-effort: interpret `value` as Linux nice value (-20..19). This affects the calling thread when using TID.
        const auto tid = static_cast<int>(::syscall(SYS_gettid));
        return ::setpriority(PRIO_PROCESS, tid, value) == 0;
#else
        (void) value;
        return false;
#endif
    }
}// namespace NGIN::Execution::ThisThread

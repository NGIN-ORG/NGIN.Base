#include <NGIN/Execution/ThisThread.hpp>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace NGIN::Execution::ThisThread
{
    std::uint32_t HardwareConcurrency() noexcept
    {
        const auto value = ::sysconf(_SC_NPROCESSORS_ONLN);
        return value <= 0 ? 1u : static_cast<std::uint32_t>(value);
    }

    ThreadId GetId() noexcept
    {
#if defined(__APPLE__)
        std::uint64_t threadId = 0;
        (void)::pthread_threadid_np(nullptr, &threadId);
        return static_cast<ThreadId>(threadId);
#else
        ThreadId       result = 0;
        const pthread_t self   = ::pthread_self();
        const auto      bytes  = std::min<std::size_t>(sizeof(result), sizeof(self));
        std::memcpy(&result, &self, bytes);
        return result;
#endif
    }

    void YieldNow() noexcept
    {
        (void)::sched_yield();
    }

    bool SetName(std::string_view name) noexcept
    {
        if (name.empty())
        {
            return false;
        }

#if defined(__APPLE__)
        std::array<char, 64> truncated {};
        const auto           length = std::min<std::size_t>(name.size(), truncated.size() - 1);
        for (std::size_t i = 0; i < length; ++i)
        {
            truncated[i] = name[i];
        }
        truncated[length] = '\0';
        return ::pthread_setname_np(truncated.data()) == 0;
#else
        (void)name;
        return false;
#endif
    }

    bool SetAffinity(UInt64 affinityMask) noexcept
    {
        (void)affinityMask;
        return false;
    }

    bool SetPriority(int value) noexcept
    {
        (void)value;
        return false;
    }
}// namespace NGIN::Execution::ThisThread

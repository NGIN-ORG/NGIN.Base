#include <NGIN/Execution/ThisThread.hpp>

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <array>

namespace NGIN::Execution::ThisThread
{
    std::uint32_t HardwareConcurrency() noexcept
    {
        const auto value = ::sysconf(_SC_NPROCESSORS_ONLN);
        return value <= 0 ? 1u : static_cast<std::uint32_t>(value);
    }

    ThreadId GetId() noexcept
    {
        return static_cast<ThreadId>(::syscall(SYS_gettid));
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

        std::array<char, 16> truncated {};
        const auto           length = std::min<std::size_t>(name.size(), truncated.size() - 1);
        for (std::size_t i = 0; i < length; ++i)
        {
            truncated[i] = name[i];
        }
        truncated[length] = '\0';
        return ::pthread_setname_np(::pthread_self(), truncated.data()) == 0;
    }

    bool SetAffinity(UInt64 affinityMask) noexcept
    {
        if (affinityMask == 0)
        {
            return false;
        }

        cpu_set_t set {};
        CPU_ZERO(&set);
        for (unsigned int bit = 0; bit < static_cast<unsigned int>(sizeof(UInt64) * 8); ++bit)
        {
            if ((affinityMask & (1ull << static_cast<UInt64>(bit))) != 0)
            {
                CPU_SET(bit, &set);
            }
        }
        return ::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set) == 0;
    }

    bool SetPriority(int value) noexcept
    {
        const auto threadId = static_cast<id_t>(::syscall(SYS_gettid));
        return ::setpriority(PRIO_PROCESS, threadId, value) == 0;
    }
}// namespace NGIN::Execution::ThisThread

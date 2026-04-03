#include <NGIN/Sync/AtomicCondition.hpp>

#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <climits>
#include <cstdint>

namespace NGIN::Sync::detail
{
    void AtomicConditionWait(UInt32& generation, UInt32 observedGeneration) noexcept
    {
        for (;;)
        {
            const int result = static_cast<int>(
                    ::syscall(SYS_futex, static_cast<std::uint32_t*>(&generation), FUTEX_WAIT_PRIVATE, observedGeneration, nullptr, nullptr, 0));
            if (result == 0 || errno == EAGAIN)
            {
                return;
            }
            if (errno != EINTR)
            {
                return;
            }
        }
    }

    bool AtomicConditionWaitFor(UInt32& generation, UInt32 observedGeneration, UInt64 nanoseconds) noexcept
    {
        timespec timeout {};
        timeout.tv_sec  = static_cast<time_t>(nanoseconds / 1'000'000'000ull);
        timeout.tv_nsec = static_cast<long>(nanoseconds % 1'000'000'000ull);

        int result = -1;
        for (;;)
        {
            result = static_cast<int>(
                    ::syscall(SYS_futex, static_cast<std::uint32_t*>(&generation), FUTEX_WAIT_PRIVATE, observedGeneration, &timeout, nullptr, 0));
            if (result == 0 || errno != EINTR)
            {
                break;
            }
        }

        if (result == 0)
        {
            return true;
        }
        if (errno == ETIMEDOUT)
        {
            return false;
        }

        return std::atomic_ref<UInt32>(generation).load(std::memory_order_acquire) != observedGeneration;
    }

    void AtomicConditionNotifyOne(UInt32& generation) noexcept
    {
        (void)generation;
        (void)::syscall(SYS_futex, static_cast<std::uint32_t*>(&generation), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
    }

    void AtomicConditionNotifyAll(UInt32& generation) noexcept
    {
        (void)generation;
        (void)::syscall(SYS_futex, static_cast<std::uint32_t*>(&generation), FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
    }
}// namespace NGIN::Sync::detail

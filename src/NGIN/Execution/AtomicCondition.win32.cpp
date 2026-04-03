#include <NGIN/Sync/AtomicCondition.hpp>

#include <Windows.h>
#include <synchapi.h>

namespace NGIN::Sync::detail
{
    void AtomicConditionWait(UInt32& generation, UInt32 observedGeneration) noexcept
    {
        (void)::WaitOnAddress(static_cast<volatile void*>(&generation), &observedGeneration, sizeof(UInt32), INFINITE);
    }

    bool AtomicConditionWaitFor(UInt32& generation, UInt32 observedGeneration, UInt64 nanoseconds) noexcept
    {
        const DWORD milliseconds = static_cast<DWORD>((nanoseconds + 999'999ull) / 1'000'000ull);
        return ::WaitOnAddress(static_cast<volatile void*>(&generation), &observedGeneration, sizeof(UInt32), milliseconds) != 0;
    }

    void AtomicConditionNotifyOne(UInt32& generation) noexcept
    {
        ::WakeByAddressSingle(static_cast<void*>(&generation));
    }

    void AtomicConditionNotifyAll(UInt32& generation) noexcept
    {
        ::WakeByAddressAll(static_cast<void*>(&generation));
    }
}// namespace NGIN::Sync::detail

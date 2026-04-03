#include <NGIN/Execution/ThisThread.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>

namespace NGIN::Execution::ThisThread
{
    std::uint32_t HardwareConcurrency() noexcept
    {
        const auto count = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        return count == 0 ? 1u : static_cast<std::uint32_t>(count);
    }

    ThreadId GetId() noexcept
    {
        return static_cast<ThreadId>(::GetCurrentThreadId());
    }

    void YieldNow() noexcept
    {
        (void)::SwitchToThread();
    }

    bool SetName(std::string_view name) noexcept
    {
        if (name.empty())
        {
            return false;
        }

        constexpr unsigned long    Flags = 0ul;
        std::array<wchar_t, 64>    wide {};
        const auto                 sourceLength = static_cast<int>(std::min<std::size_t>(name.size(), wide.size() - 1));
        const int                  written =
                ::MultiByteToWideChar(CP_UTF8, Flags, name.data(), sourceLength, wide.data(), static_cast<int>(wide.size() - 1));
        if (written <= 0)
        {
            return false;
        }

        wide[static_cast<std::size_t>(written)] = L'\0';
        return ::SetThreadDescription(::GetCurrentThread(), wide.data()) >= 0;
    }

    bool SetAffinity(UInt64 affinityMask) noexcept
    {
        if (affinityMask == 0)
        {
            return false;
        }

        return ::SetThreadAffinityMask(::GetCurrentThread(), static_cast<std::uintptr_t>(affinityMask)) != 0;
    }

    bool SetPriority(int value) noexcept
    {
        return ::SetThreadPriority(::GetCurrentThread(), value) != 0;
    }
}// namespace NGIN::Execution::ThisThread

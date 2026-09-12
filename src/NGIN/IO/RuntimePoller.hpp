#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/IO/NativeWaitSource.hpp>
#include <NGIN/Time/TimePoint.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

namespace NGIN::IO::detail
{
    // Policy-free platform waiting. The consumer maps generation identifiers
    // to live registrations; the OS never stores pointers into coroutine frames.
    class NGIN_IORUNTIME_API RuntimePoller final
    {
    public:
        enum Interest : unsigned
        {
            Read  = 1,
            Write = 2,
            Error = 4
        };
        struct Event
        {
            std::uint64_t identifier {};
            unsigned      ready {};
            void*         operation {};// IOCP OVERLAPPED, otherwise null.
            std::uint32_t bytes {};
            int           error {};
        };

        explicit RuntimePoller(std::size_t capacity);
        ~RuntimePoller();
        RuntimePoller(const RuntimePoller&)            = delete;
        RuntimePoller& operator=(const RuntimePoller&) = delete;

        // Thread-safe. Identifier zero is reserved for control wakeups.
        std::error_code Watch(std::uintptr_t handle, std::uint64_t identifier, unsigned interests) noexcept;
        void            Unwatch(std::uintptr_t handle) noexcept;
        // IOCP registrations belong to individual OVERLAPPED requests. The request
        // must remain alive until its terminal packet is consumed and unregistered.
        // Other pollers return operation_not_supported.
        std::error_code WatchCompletion(std::uintptr_t handle, void* operation, std::uint64_t identifier) noexcept;
        void            UnwatchCompletion(void* operation) noexcept;
        void            Wake() noexcept;

        // One consumer. nullopt waits indefinitely; an expired deadline polls.
        // Always rescan source readiness after this returns, including zero events.
        std::size_t Wait(std::span<Event> events, std::optional<NGIN::Time::TimePoint> deadline);
        // epoll/kqueue descriptor, IOCP handle, or -1 on the portable poll fallback.
        std::intptr_t NativeHandle() const noexcept;
        std::expected<std::size_t, std::error_code>
        CopyNativeWaitSources(std::span<NativeWaitSource> destination) const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::IO::detail

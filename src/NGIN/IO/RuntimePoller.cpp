#include "RuntimePoller.hpp"

#include <NGIN/Time/MonotonicClock.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

// Exercise the supported poll fallback in Linux tests without changing the
// production backend selection or exposing a public backend plugin mechanism.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) && !defined(NGIN_RUNTIME_POLLER_PORTABLE)
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#include <sys/event.h>
#endif
#endif

namespace NGIN::IO::detail
{
    namespace
    {
        int Timeout(std::optional<NGIN::Time::TimePoint> deadline) noexcept
        {
            if (!deadline)
                return -1;
            const NGIN::UInt64 now = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            if (deadline->ToNanoseconds() <= now)
                return 0;
            const NGIN::UInt64 remaining    = deadline->ToNanoseconds() - now;
            const NGIN::UInt64 milliseconds = remaining / 1'000'000 + (remaining % 1'000'000 != 0);
            return static_cast<int>((std::min) (milliseconds, static_cast<NGIN::UInt64>(INT_MAX)));
        }
    }// namespace

    struct RuntimePoller::Impl
    {
        explicit Impl(std::size_t capacity)
#if defined(_WIN32)
            : capacity(capacity)
#endif
        {
            if (capacity == 0)
                throw std::invalid_argument("Runtime poller requires positive registration capacity");
#if defined(_WIN32)
            port = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
            if (!port)
                throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "CreateIoCompletionPort");
#elif defined(__linux__) && !defined(NGIN_RUNTIME_POLLER_PORTABLE)
            poller = ::epoll_create1(EPOLL_CLOEXEC);
            if (poller < 0)
                throw std::system_error(errno, std::generic_category(), "epoll_create1");
            wakeRead = wakeWrite = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (wakeRead >= 0)
            {
                epoll_event event {};
                event.events   = EPOLLIN;
                event.data.u64 = 0;
                if (::epoll_ctl(poller, EPOLL_CTL_ADD, wakeRead, &event) == 0)
                    return;
            }
            const int error = errno;
            if (wakeRead >= 0)
                ::close(wakeRead);
            ::close(poller);
            throw std::system_error(error, std::generic_category(), "initialize runtime eventfd");
#else
            // Allocate fallback wait snapshots before accepting registrations.
            registrations.reserve(capacity);
            snapshot.reserve(capacity + 1);
            identifiers.reserve(capacity + 1);
            int descriptors[2];
            if (::pipe(descriptors) != 0)
                throw std::system_error(errno, std::generic_category(), "runtime wake pipe");
            wakeRead  = descriptors[0];
            wakeWrite = descriptors[1];
            for (int descriptor: descriptors)
            {
                if (::fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0 ||
                    ::fcntl(descriptor, F_SETFL, O_NONBLOCK) < 0)
                {
                    const int error = errno;
                    ::close(wakeRead);
                    ::close(wakeWrite);
                    throw std::system_error(error, std::generic_category(), "configure runtime wake pipe");
                }
            }
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
            poller = ::kqueue();
            if (poller >= 0 && ::fcntl(poller, F_SETFD, FD_CLOEXEC) == 0)
            {
                struct kevent event;
                EV_SET(&event, wakeRead, EVFILT_READ, EV_ADD, 0, 0, nullptr);
                if (::kevent(poller, &event, 1, nullptr, 0, nullptr) == 0)
                    return;
            }
            const int error = errno;
            if (poller >= 0)
                ::close(poller);
            ::close(wakeRead);
            ::close(wakeWrite);
            throw std::system_error(error, std::generic_category(), "initialize runtime kqueue");
#endif
#endif
        }

        ~Impl()
        {
#if defined(_WIN32)
            ::CloseHandle(port);
#else
            if (poller >= 0)
                ::close(poller);
            if (wakeRead >= 0)
                ::close(wakeRead);
            if (wakeWrite >= 0 && wakeWrite != wakeRead)
                ::close(wakeWrite);
#endif
        }

        void DrainWake() noexcept
        {
#if defined(__linux__) && !defined(NGIN_RUNTIME_POLLER_PORTABLE)
            std::uint64_t value;
            while (::read(wakeRead, &value, sizeof(value)) < 0 && errno == EINTR) {}
#elif !defined(_WIN32)
            std::array<char, 128> bytes;
            for (;;)
            {
                const auto count = ::read(wakeRead, bytes.data(), bytes.size());
                if (count > 0 || (count < 0 && errno == EINTR))
                    continue;
                break;
            }
#endif
            // The loop rescans all queue/deadline state after draining this
            // signal. A producer after this store creates a fresh notification.
            wakePending.store(false, std::memory_order_release);
        }

        std::atomic<bool> wakePending {false};
#if defined(_WIN32)
        HANDLE                                   port {};
        const std::size_t                        capacity;
        std::mutex                               mutex;
        std::unordered_map<void*, std::uint64_t> operations;
#else
        int poller {-1};
        int wakeRead {-1};
        int wakeWrite {-1};
#if !defined(__linux__) || defined(NGIN_RUNTIME_POLLER_PORTABLE)
        struct Registration
        {
            int           descriptor;
            std::uint64_t identifier;
            unsigned      interests;
        };
        std::mutex                 mutex;
        std::vector<Registration>  registrations;
        std::vector<pollfd>        snapshot;
        std::vector<std::uint64_t> identifiers;
        std::size_t                pollCursor {};
#endif
#endif
    };

    RuntimePoller::RuntimePoller(std::size_t capacity) : m_impl(std::make_unique<Impl>(capacity)) {}
    RuntimePoller::~RuntimePoller() = default;

    std::error_code RuntimePoller::Watch(std::uintptr_t handle, std::uint64_t identifier, unsigned interests) noexcept
    {
        if (identifier == 0 || (interests & (Read | Write)) == 0)
            return std::make_error_code(std::errc::invalid_argument);
#if defined(_WIN32)
        (void) handle;
        return std::make_error_code(std::errc::operation_not_supported);
#elif defined(__linux__) && !defined(NGIN_RUNTIME_POLLER_PORTABLE)
        epoll_event event {};
        event.data.u64 = identifier;
        event.events   = (interests & Read ? EPOLLIN : 0U) | (interests & Write ? EPOLLOUT : 0U);
        if (::epoll_ctl(m_impl->poller, EPOLL_CTL_ADD, static_cast<int>(handle), &event) != 0)
        {
            if (errno != EEXIST || ::epoll_ctl(m_impl->poller, EPOLL_CTL_MOD, static_cast<int>(handle), &event) != 0)
                return {errno, std::generic_category()};
        }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
        {
            std::lock_guard lock(m_impl->mutex);
            const auto      found = std::find_if(m_impl->registrations.begin(), m_impl->registrations.end(),
                                                 [handle](const Impl::Registration& entry) { return entry.descriptor == static_cast<int>(handle); });
            if (found == m_impl->registrations.end() && m_impl->registrations.size() == m_impl->registrations.capacity())
                return std::make_error_code(std::errc::no_buffer_space);
            const auto apply = [&](std::uint64_t generation, unsigned selected) noexcept -> std::error_code {
                const std::array<short, 2> filters {EVFILT_READ, EVFILT_WRITE};
                for (std::size_t index = 0; index < filters.size(); ++index)
                {
                    struct kevent event;
                    const bool    enabled = (selected & (index == 0 ? Read : Write)) != 0;
                    EV_SET(&event, handle, filters[index], enabled ? EV_ADD : EV_DELETE, 0, 0,
                           reinterpret_cast<void*>(static_cast<std::uintptr_t>(generation)));
                    if (::kevent(m_impl->poller, &event, 1, nullptr, 0, nullptr) < 0 && (enabled || errno != ENOENT))
                        return {errno, std::generic_category()};
                }
                return {};
            };
            if (const std::error_code error = apply(identifier, interests))
            {
                // A change to the second filter can fail after the first was
                // accepted. Restore the previous registration before reporting it.
                (void) apply(found == m_impl->registrations.end() ? identifier : found->identifier,
                             found == m_impl->registrations.end() ? 0U : found->interests);
                return error;
            }
            if (found == m_impl->registrations.end())
                m_impl->registrations.push_back({static_cast<int>(handle), identifier, interests});
            else
                *found = {static_cast<int>(handle), identifier, interests};
        }
#else
        {
            std::lock_guard lock(m_impl->mutex);
            auto            found = std::find_if(m_impl->registrations.begin(), m_impl->registrations.end(),
                                                 [handle](const Impl::Registration& entry) { return entry.descriptor == static_cast<int>(handle); });
            if (found != m_impl->registrations.end())
                *found = {static_cast<int>(handle), identifier, interests};
            else
            {
                if (m_impl->registrations.size() == m_impl->registrations.capacity())
                    return std::make_error_code(std::errc::no_buffer_space);
                m_impl->registrations.push_back({static_cast<int>(handle), identifier, interests});
            }
        }
#endif
        Wake();
        return {};
    }

    void RuntimePoller::Unwatch(std::uintptr_t handle) noexcept
    {
#if defined(__linux__) && !defined(NGIN_RUNTIME_POLLER_PORTABLE)
        (void) ::epoll_ctl(m_impl->poller, EPOLL_CTL_DEL, static_cast<int>(handle), nullptr);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
        std::lock_guard lock(m_impl->mutex);
        for (short filter: {EVFILT_READ, EVFILT_WRITE})
        {
            struct kevent event;
            EV_SET(&event, handle, filter, EV_DELETE, 0, 0, nullptr);
            (void) ::kevent(m_impl->poller, &event, 1, nullptr, 0, nullptr);
        }
        std::erase_if(m_impl->registrations,
                      [handle](const Impl::Registration& entry) { return entry.descriptor == static_cast<int>(handle); });
#elif !defined(_WIN32)
        std::lock_guard lock(m_impl->mutex);
        std::erase_if(m_impl->registrations,
                      [handle](const Impl::Registration& entry) { return entry.descriptor == static_cast<int>(handle); });
#else
        (void) handle;// IOCP association ends when the OS handle is closed.
#endif
        Wake();
    }

    std::error_code RuntimePoller::WatchCompletion(std::uintptr_t handle, void* operation, std::uint64_t identifier) noexcept
    {
        if (!operation || identifier == 0 || handle == 0 || handle == static_cast<std::uintptr_t>(-1))
            return std::make_error_code(std::errc::invalid_argument);
#if defined(_WIN32)
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->operations.contains(operation))
            return std::make_error_code(std::errc::device_or_resource_busy);
        if (m_impl->operations.size() == m_impl->capacity)
            return std::make_error_code(std::errc::no_buffer_space);
        try
        {
            m_impl->operations.emplace(operation, identifier);
        } catch (const std::bad_alloc&)
        {
            return std::make_error_code(std::errc::not_enough_memory);
        }
        // Completion keys cannot change when a live handle is associated again.
        // Use a fixed key and identify each request through its stable OVERLAPPED.
        if (::CreateIoCompletionPort(reinterpret_cast<HANDLE>(handle), m_impl->port, 0, 0) != m_impl->port)
        {
            const int error = static_cast<int>(::GetLastError());
            m_impl->operations.erase(operation);
            return {error, std::system_category()};
        }
        return {};
#else
        (void) handle;
        return std::make_error_code(std::errc::operation_not_supported);
#endif
    }

    void RuntimePoller::UnwatchCompletion(void* operation) noexcept
    {
#if defined(_WIN32)
        std::lock_guard lock(m_impl->mutex);
        m_impl->operations.erase(operation);
#else
        (void) operation;
#endif
        Wake();
    }

    void RuntimePoller::Wake() noexcept
    {
        if (m_impl->wakePending.exchange(true, std::memory_order_acq_rel))
            return;
#if defined(_WIN32)
        if (!::PostQueuedCompletionStatus(m_impl->port, 0, 0, nullptr))
            std::terminate();// Losing an accepted completion's wakeup is not recoverable here.
#elif defined(__linux__) && !defined(NGIN_RUNTIME_POLLER_PORTABLE)
        const std::uint64_t value = 1;
        while (::write(m_impl->wakeWrite, &value, sizeof(value)) < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN)
                std::terminate();
            break;
        }
#else
        const char value = 1;
        while (::write(m_impl->wakeWrite, &value, sizeof(value)) < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN)
                std::terminate();
            break;
        }
#endif
    }

    std::size_t RuntimePoller::Wait(std::span<Event> events, std::optional<NGIN::Time::TimePoint> deadline)
    {
        if (events.empty())
            return 0;
        std::size_t count = 0;
#if defined(_WIN32)
        do
        {
            DWORD       bytes     = 0;
            ULONG_PTR   key       = 0;
            OVERLAPPED* operation = nullptr;
            const int   timeout   = count == 0 ? Timeout(deadline) : 0;
            const BOOL  success   = ::GetQueuedCompletionStatus(m_impl->port, &bytes, &key, &operation,
                                                             timeout < 0 ? INFINITE : static_cast<DWORD>(timeout));
            const DWORD error     = success ? ERROR_SUCCESS : ::GetLastError();
            if (!operation && key == 0)
            {
                if (success)
                    m_impl->DrainWake();
                else if (error != WAIT_TIMEOUT)
                    throw std::system_error(static_cast<int>(error), std::system_category(), "runtime IOCP wait");
                break;
            }
            std::uint64_t identifier = 0;
            {
                std::lock_guard lock(m_impl->mutex);
                const auto      found = m_impl->operations.find(operation);
                if (found != m_impl->operations.end())
                    identifier = found->second;
            }
            // Unknown packets are stale. Preserve the batch bound even when a
            // packet no longer addresses a registration; zero is ignored upstream.
            events[count++] = {identifier, Read | Write, operation, bytes, static_cast<int>(error)};
        } while (count < events.size());
#elif defined(__linux__) && !defined(NGIN_RUNTIME_POLLER_PORTABLE)
        std::array<epoll_event, 64> native {};
        int                         result;
        do
        {
            result = ::epoll_wait(m_impl->poller, native.data(), static_cast<int>((std::min) (native.size(), events.size())), Timeout(deadline));
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            throw std::system_error(errno, std::generic_category(), "runtime epoll_wait");
        for (int index = 0; index < result; ++index)
        {
            const epoll_event& event = native[static_cast<std::size_t>(index)];
            if (event.data.u64 == 0)
                m_impl->DrainWake();
            else
                events[count++] = {event.data.u64, (event.events & EPOLLIN ? Read : 0U) |
                                                           (event.events & EPOLLOUT ? Write : 0U) | (event.events & (EPOLLERR | EPOLLHUP) ? Error : 0U)};
        }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
        std::array<struct kevent, 64> native {};
        int                           result;
        do
        {
            const int      milliseconds = Timeout(deadline);
            const timespec timeout {milliseconds < 0 ? 0 : milliseconds / 1000,
                                    milliseconds < 0 ? 0L : static_cast<long>(milliseconds % 1000) * 1'000'000};
            result = ::kevent(m_impl->poller, nullptr, 0, native.data(),
                              static_cast<int>((std::min) (native.size(), events.size())), milliseconds < 0 ? nullptr : &timeout);
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            throw std::system_error(errno, std::generic_category(), "runtime kevent wait");
        for (int index = 0; index < result; ++index)
        {
            const auto&         event      = native[static_cast<std::size_t>(index)];
            const std::uint64_t identifier = reinterpret_cast<std::uintptr_t>(event.udata);
            if (identifier == 0)
                m_impl->DrainWake();
            else
                events[count++] = {identifier, (event.filter == EVFILT_READ ? Read : Write) |
                                                       (event.flags & (EV_EOF | EV_ERROR) ? Error : 0U)};
        }
#else
        // The portable poll fallback scans the registration snapshot. Native
        // epoll/kqueue/IOCP dispatch above scales with returned ready events.
        {
            std::lock_guard lock(m_impl->mutex);
            m_impl->snapshot.clear();
            m_impl->identifiers.clear();
            m_impl->snapshot.push_back({m_impl->wakeRead, POLLIN, 0});
            m_impl->identifiers.push_back(0);
            for (const Impl::Registration& registration: m_impl->registrations)
            {
                m_impl->snapshot.push_back({registration.descriptor, static_cast<short>((registration.interests & Read ? POLLIN : 0) | (registration.interests & Write ? POLLOUT : 0)), 0});
                m_impl->identifiers.push_back(registration.identifier);
            }
        }
        int result;
        do
        {
            result = ::poll(m_impl->snapshot.data(), static_cast<nfds_t>(m_impl->snapshot.size()), Timeout(deadline));
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            throw std::system_error(errno, std::generic_category(), "runtime poll");
        if (m_impl->snapshot[0].revents)
            m_impl->DrainWake();
        const std::size_t descriptors = m_impl->snapshot.size() - 1;
        for (std::size_t examined = 0; examined < descriptors && count < events.size(); ++examined)
        {
            const std::size_t index = 1 + m_impl->pollCursor++ % descriptors;
            const short       ready = m_impl->snapshot[index].revents;
            if (!ready)
                continue;
            events[count++] = {m_impl->identifiers[index], (ready & POLLIN ? Read : 0U) |
                                                                   (ready & POLLOUT ? Write : 0U) | (ready & (POLLERR | POLLHUP | POLLNVAL) ? Error : 0U)};
        }
#endif
        return count;
    }

    std::intptr_t RuntimePoller::NativeHandle() const noexcept
    {
#if defined(_WIN32)
        return reinterpret_cast<std::intptr_t>(m_impl->port);
#else
        return m_impl->poller;
#endif
    }

    std::expected<std::size_t, std::error_code>
    RuntimePoller::CopyNativeWaitSources(std::span<NativeWaitSource> destination) const noexcept
    {
#if defined(_WIN32)
        (void) destination;
        return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
#else
        if (m_impl->poller >= 0)
        {
            if (destination.empty())
                return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
            destination[0] = {m_impl->poller, NativeWaitSource::Read};
            return 1;
        }
#if !defined(__linux__) || defined(NGIN_RUNTIME_POLLER_PORTABLE)
        std::lock_guard   lock(m_impl->mutex);
        const std::size_t count = m_impl->registrations.size() + 1;
        if (destination.size() < count)
            return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        destination[0] = {m_impl->wakeRead, NativeWaitSource::Read};
        for (std::size_t index = 0; index != m_impl->registrations.size(); ++index)
        {
            const auto& entry      = m_impl->registrations[index];
            destination[index + 1] = {entry.descriptor,
                                      (entry.interests & Read ? NativeWaitSource::Read : 0U) |
                                              (entry.interests & Write ? NativeWaitSource::Write : 0U)};
        }
        return count;
#else
        std::terminate();// A successfully constructed epoll backend always has its descriptor.
#endif
#endif
    }
}// namespace NGIN::IO::detail

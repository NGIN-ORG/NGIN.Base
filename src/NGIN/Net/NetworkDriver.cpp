#include <NGIN/Net/Runtime/NetworkDriver.hpp>

#include "SocketPlatform.hpp"

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Execution/ThreadName.hpp>
#include <NGIN/Execution/ThisThread.hpp>

#if defined(NGIN_PLATFORM_WINDOWS)
#include <Windows.h>
#include <ioapiset.h>
#include <NGIN/Net/Sockets/UdpSocket.hpp>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <sys/epoll.h>
#include <unistd.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace NGIN::Net
{
    [[nodiscard]] static NGIN::Async::AsyncError ToAsyncError(NetError error) noexcept
    {
        if (error.code == NetErrorCode::Ok)
        {
            return NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::Ok);
        }
        const int native = (error.native != 0) ? error.native : static_cast<int>(error.code);
        return NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::Fault, native);
    }

    [[nodiscard]] NGIN::Async::AsyncError MakeCanceledError() noexcept
    {
        return NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::Canceled);
    }

    struct NetworkDriver::Impl final
    {
        struct Waiter final
        {
            Impl*                                 owner {nullptr};
            SocketHandle*                         handle {nullptr};
            bool                                  wantRead {false};
            bool                                  wantWrite {false};
            NGIN::Execution::ExecutorRef          exec {};
            std::coroutine_handle<>               continuation {};
            NGIN::Async::CancellationRegistration cancellation {};
            std::atomic<bool>                     done {false};
        };

#if defined(NGIN_PLATFORM_WINDOWS)
        struct IocpOperation final
        {
            WSAOVERLAPPED                         overlapped {};
            WSABUF                                buffer {};
            SocketHandle*                         handle {nullptr};
            NGIN::Execution::ExecutorRef          exec {};
            std::coroutine_handle<>               continuation {};
            NGIN::Async::CancellationRegistration cancellation {};
            std::atomic<bool>                     done {false};
            NetError                              error {};
            DWORD                                 bytes {0};
            DWORD                                 flags {0};
            sockaddr_storage                      address {};
            int                                   addressLength {0};
            bool                                  skipCompletionOnSuccess {false};
        };
#endif

        explicit Impl(NetworkDriverOptions options)
            : m_options(options)
        {
#if defined(NGIN_PLATFORM_WINDOWS)
            m_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
#endif
#if defined(__linux__)
            m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
            m_kqueueFd = ::kqueue();
#endif
        }

        ~Impl()
        {
#if defined(NGIN_PLATFORM_WINDOWS)
            if (m_iocp)
            {
                ::CloseHandle(m_iocp);
                m_iocp = nullptr;
            }
#endif
#if defined(__linux__)
            if (m_epollFd >= 0)
            {
                ::close(m_epollFd);
                m_epollFd = -1;
            }
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
            if (m_kqueueFd >= 0)
            {
                ::close(m_kqueueFd);
                m_kqueueFd = -1;
            }
#endif
        }

        void RegisterWaiter(Waiter* waiter)
        {
            std::lock_guard guard(m_mutex);
            m_waiters.push_back(waiter);
#if defined(__linux__)
            UpdateEpollOnRegisterLocked(waiter);
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
            UpdateKqueueOnRegisterLocked(waiter);
#endif
        }

        void UnregisterWaiter(Waiter* waiter)
        {
            std::lock_guard guard(m_mutex);
            auto            it = std::find(m_waiters.begin(), m_waiters.end(), waiter);
            if (it != m_waiters.end())
            {
                *it = m_waiters.back();
                m_waiters.pop_back();
            }
#if defined(__linux__)
            UpdateEpollOnUnregisterLocked(waiter);
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
            UpdateKqueueOnUnregisterLocked(waiter);
#endif
        }

#if defined(NGIN_PLATFORM_WINDOWS)
        static bool CancelIocp(void* ctx) noexcept
        {
            auto* op = static_cast<IocpOperation*>(ctx);
            if (!op || !op->handle)
            {
                return false;
            }

            if (op->done.load(std::memory_order_acquire))
            {
                return false;
            }

            const auto sock = detail::ToNative(*op->handle);
            if (sock == detail::InvalidNativeSocket)
            {
                return false;
            }

            ::CancelIoEx(reinterpret_cast<HANDLE>(sock), reinterpret_cast<LPOVERLAPPED>(&op->overlapped));
            return false;
        }

        bool EnsureAssociated(SocketHandle& handle) noexcept
        {
            if (!m_iocp)
            {
                return false;
            }

            const auto sock = detail::ToNative(handle);
            if (sock == detail::InvalidNativeSocket)
            {
                return false;
            }

            const auto result = ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock), m_iocp, 0, 0);
            if (result != nullptr)
            {
                return true;
            }

            const DWORD err = ::GetLastError();
            return err == ERROR_INVALID_PARAMETER;
        }

        bool TrySkipCompletionOnSuccess(SocketHandle& handle) noexcept
        {
            const auto sock = detail::ToNative(handle);
            if (sock == detail::InvalidNativeSocket)
            {
                return false;
            }

            const UCHAR flags = FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE;
            const BOOL  ok    = ::SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(sock), flags);
            return ok != FALSE;
        }

        void CompleteOperation(IocpOperation& op, DWORD bytes, DWORD error) noexcept
        {
            bool expected = false;
            if (!op.done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return;
            }

            op.bytes = bytes;
            if (error == 0)
            {
                op.error = NetError {NetErrorCode::Ok, 0};
            }
            else
            {
                op.error = detail::MapError(static_cast<int>(error));
            }
            op.cancellation.Reset();

            if (op.exec.IsValid())
            {
                op.exec.Execute(op.continuation);
            }
            else if (op.continuation)
            {
                op.continuation.resume();
            }
        }

        void CompleteOperationWithError(IocpOperation& op, DWORD bytes, NetError error) noexcept
        {
            bool expected = false;
            if (!op.done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return;
            }

            op.bytes = bytes;
            op.error = error;
            op.cancellation.Reset();

            if (op.exec.IsValid())
            {
                op.exec.Execute(op.continuation);
            }
            else if (op.continuation)
            {
                op.continuation.resume();
            }
        }

        void PumpIocp(DWORD timeoutMs) noexcept
        {
            if (!m_iocp)
            {
                return;
            }

            DWORD        bytes      = 0;
            ULONG_PTR    key        = 0;
            LPOVERLAPPED overlapped = nullptr;
            const BOOL   ok         = ::GetQueuedCompletionStatus(m_iocp, &bytes, &key, &overlapped, timeoutMs);
            if (!overlapped)
            {
                return;
            }

            DWORD error = ok ? 0 : ::GetLastError();
            if (auto* op = reinterpret_cast<IocpOperation*>(overlapped))
            {
                CompleteOperation(*op, bytes, error);
            }

            for (int i = 0; i < 63; ++i)
            {
                bytes = 0;
                key = 0;
                overlapped = nullptr;
                const BOOL drainOk = ::GetQueuedCompletionStatus(m_iocp, &bytes, &key, &overlapped, 0);
                if (!overlapped)
                {
                    break;
                }
                error = drainOk ? 0 : ::GetLastError();
                if (auto* op = reinterpret_cast<IocpOperation*>(overlapped))
                {
                    CompleteOperation(*op, bytes, error);
                }
            }
        }
#endif

        void CompleteWaiter(Waiter* waiter) noexcept
        {
            if (!waiter)
            {
                return;
            }

            bool expected = false;
            if (!waiter->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return;
            }

            UnregisterWaiter(waiter);
            waiter->cancellation.Reset();

            if (waiter->exec.IsValid())
            {
                waiter->exec.Execute(waiter->continuation);
            }
            else if (waiter->continuation)
            {
                waiter->continuation.resume();
            }
        }

        void PollOnce(int timeoutMs)
        {
            thread_local int s_pollDepth = 0;
            struct DepthGuard final
            {
                int& depth;
                ~DepthGuard() { --depth; }
            };

            const bool useThreadLocal = (s_pollDepth == 0);
            ++s_pollDepth;
            DepthGuard depthGuard {s_pollDepth};

#if defined(NGIN_PLATFORM_WINDOWS)
            const DWORD waitMs = timeoutMs > 0 ? static_cast<DWORD>(timeoutMs) : 0;
#endif
            thread_local std::vector<Waiter*> tlsWaiters {};
            std::vector<Waiter*>              localWaiters {};
            auto&                             waiters = useThreadLocal ? tlsWaiters : localWaiters;
            {
                std::lock_guard guard(m_mutex);
                if (!m_waiters.empty())
                {
                    waiters.clear();
                    waiters.reserve(m_waiters.size());
                    waiters.insert(waiters.end(), m_waiters.begin(), m_waiters.end());
                }
                else
                {
                    waiters.clear();
                }
            }

            std::size_t validCount = 0;
            for (auto* waiter: waiters)
            {
                if (!waiter || !waiter->handle)
                {
                    CompleteWaiter(waiter);
                    continue;
                }

                const auto sock = detail::ToNative(*waiter->handle);
                if (sock == detail::InvalidNativeSocket)
                {
                    CompleteWaiter(waiter);
                    continue;
                }

                waiters[validCount++] = waiter;
            }
            waiters.resize(validCount);

#if defined(NGIN_PLATFORM_WINDOWS)
            if (waiters.empty())
            {
                PumpIocp(waitMs);
                return;
            }

            PumpIocp(0);
#else
            if (waiters.empty())
            {
                if (timeoutMs > 0)
                {
                    NGIN::Execution::ThisThread::SleepFor(
                            NGIN::Units::Milliseconds(static_cast<double>(timeoutMs)));
                }
                return;
            }
#endif

#if defined(__linux__)
            if (m_epollFd >= 0)
            {
                std::array<epoll_event, 64> events {};
                const int timeout = timeoutMs > 0 ? timeoutMs : 0;
                const int ready = ::epoll_wait(m_epollFd,
                                               events.data(),
                                               static_cast<int>(events.size()),
                                               timeout);
                if (ready <= 0)
                {
                    return;
                }

                thread_local std::unordered_map<int, std::uint32_t> tlsReadyEvents {};
                std::unordered_map<int, std::uint32_t>              localReadyEvents {};
                auto&                                               readyEvents = useThreadLocal ? tlsReadyEvents : localReadyEvents;
                readyEvents.clear();
                readyEvents.reserve(static_cast<std::size_t>(ready));
                for (int i = 0; i < ready; ++i)
                {
                    readyEvents[events[i].data.fd] |= events[i].events;
                }

                for (auto* waiter: waiters)
                {
                    if (!waiter || !waiter->handle)
                    {
                        CompleteWaiter(waiter);
                        continue;
                    }
                    const auto sock = detail::ToNative(*waiter->handle);
                    if (sock == detail::InvalidNativeSocket)
                    {
                        CompleteWaiter(waiter);
                        continue;
                    }

                    const auto it = readyEvents.find(sock);
                    if (it == readyEvents.end())
                    {
                        continue;
                    }

                    const auto eventsMask = it->second;
                    const bool readyRead  = waiter->wantRead &&
                                           (eventsMask & (EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLRDHUP));
                    const bool readyWrite = waiter->wantWrite && (eventsMask & (EPOLLOUT | EPOLLERR));
                    if (!readyRead && !readyWrite)
                    {
                        continue;
                    }

                    CompleteWaiter(waiter);
                }
                return;
            }
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
            if (m_kqueueFd >= 0)
            {
                std::array<kevent, 64> events {};
                timespec timeout {};
                if (timeoutMs > 0)
                {
                    timeout.tv_sec = timeoutMs / 1000;
                    timeout.tv_nsec = (timeoutMs % 1000) * 1000000L;
                }
                const int              ready = ::kevent(m_kqueueFd,
                                            nullptr,
                                            0,
                                            events.data(),
                                            static_cast<int>(events.size()),
                                            &timeout);
                if (ready <= 0)
                {
                    return;
                }

                struct KqueueReady final
                {
                    bool read {false};
                    bool write {false};
                };

                thread_local std::unordered_map<int, KqueueReady> tlsReadyEvents {};
                std::unordered_map<int, KqueueReady>              localReadyEvents {};
                auto&                                            readyEvents = useThreadLocal ? tlsReadyEvents : localReadyEvents;
                readyEvents.clear();
                readyEvents.reserve(static_cast<std::size_t>(ready));
                for (int i = 0; i < ready; ++i)
                {
                    const auto fd = static_cast<int>(events[i].ident);
                    auto& entry = readyEvents[fd];
                    if (events[i].filter == EVFILT_READ)
                    {
                        entry.read = true;
                    }
                    if (events[i].filter == EVFILT_WRITE)
                    {
                        entry.write = true;
                    }
                    if (events[i].flags & (EV_EOF | EV_ERROR))
                    {
                        entry.read = true;
                        entry.write = true;
                    }
                }

                for (auto* waiter: waiters)
                {
                    if (!waiter || !waiter->handle)
                    {
                        CompleteWaiter(waiter);
                        continue;
                    }
                    const auto sock = detail::ToNative(*waiter->handle);
                    if (sock == detail::InvalidNativeSocket)
                    {
                        CompleteWaiter(waiter);
                        continue;
                    }

                    const auto it = readyEvents.find(sock);
                    if (it == readyEvents.end())
                    {
                        continue;
                    }

                    const bool readyRead = waiter->wantRead && it->second.read;
                    const bool readyWrite = waiter->wantWrite && it->second.write;
                    if (!readyRead && !readyWrite)
                    {
                        continue;
                    }

                    CompleteWaiter(waiter);
                }
                return;
            }
#endif

            fd_set readSet {};
            fd_set writeSet {};
            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);

#if !defined(NGIN_PLATFORM_WINDOWS)
            int maxFd = -1;
#endif
            for (auto* waiter: waiters)
            {
                if (!waiter || !waiter->handle)
                {
                    continue;
                }
                const auto sock = detail::ToNative(*waiter->handle);
                if (sock == detail::InvalidNativeSocket)
                {
                    continue;
                }
                if (waiter->wantRead)
                {
                    FD_SET(sock, &readSet);
                }
                if (waiter->wantWrite)
                {
                    FD_SET(sock, &writeSet);
                }
#if !defined(NGIN_PLATFORM_WINDOWS)
                if (sock > maxFd)
                {
                    maxFd = sock;
                }
#endif
            }

            timeval timeout {};
            if (timeoutMs > 0)
            {
                timeout.tv_sec = timeoutMs / 1000;
                timeout.tv_usec = (timeoutMs % 1000) * 1000;
            }

#if defined(NGIN_PLATFORM_WINDOWS)
            const int ready = ::select(0, &readSet, &writeSet, nullptr, &timeout);
#else
            const int ready = (maxFd >= 0) ? ::select(maxFd + 1, &readSet, &writeSet, nullptr, &timeout) : 0;
#endif
            if (ready <= 0)
            {
                return;
            }

            for (auto* waiter: waiters)
            {
                if (!waiter || !waiter->handle)
                {
                    CompleteWaiter(waiter);
                    continue;
                }
                const auto sock = detail::ToNative(*waiter->handle);
                if (sock == detail::InvalidNativeSocket)
                {
                    CompleteWaiter(waiter);
                    continue;
                }

                const bool readyRead  = waiter->wantRead && FD_ISSET(sock, &readSet);
                const bool readyWrite = waiter->wantWrite && FD_ISSET(sock, &writeSet);
                if (!readyRead && !readyWrite)
                {
                    continue;
                }

                CompleteWaiter(waiter);
            }
        }

        void Run()
        {
            const int timeoutMs = GetPollTimeoutMs();
            if (m_options.workerThreads == 0)
            {
                while (!m_stop.load(std::memory_order_acquire))
                {
                    PollOnce(timeoutMs);
                }
                return;
            }

            StartWorkers();
            JoinWorkers();
        }

        void Stop() noexcept
        {
            m_stop.store(true, std::memory_order_release);
        }

        void Shutdown() noexcept
        {
            Stop();
            JoinWorkers();
        }

        NetworkDriverOptions                 m_options {};
        std::mutex                           m_mutex {};
        std::vector<Waiter*>                 m_waiters {};
        std::atomic<bool>                    m_stop {false};
        std::vector<NGIN::Execution::Thread> m_workers {};
#if defined(NGIN_PLATFORM_WINDOWS)
        HANDLE m_iocp {nullptr};
#endif
#if defined(__linux__)
        struct EpollWatch final
        {
            std::uint32_t events {0};
            int           readers {0};
            int           writers {0};
        };

        int                                 m_epollFd {-1};
        std::unordered_map<int, EpollWatch> m_epollWatches {};
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
        struct KqueueWatch final
        {
            int readers {0};
            int writers {0};
        };

        int                                   m_kqueueFd {-1};
        std::unordered_map<int, KqueueWatch>  m_kqueueWatches {};
#endif

    private:
#if defined(__linux__)
        void UpdateEpollOnRegisterLocked(Waiter* waiter) noexcept
        {
            if (m_epollFd < 0 || !waiter || !waiter->handle)
            {
                return;
            }

            const int fd = detail::ToNative(*waiter->handle);
            if (fd == detail::InvalidNativeSocket)
            {
                return;
            }

            auto&               watch      = m_epollWatches[fd];
            const std::uint32_t prevEvents = watch.events;

            if (waiter->wantRead)
            {
                ++watch.readers;
            }
            if (waiter->wantWrite)
            {
                ++watch.writers;
            }

            std::uint32_t events = 0;
            if (watch.readers > 0)
            {
                events |= EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
            }
            if (watch.writers > 0)
            {
                events |= EPOLLOUT | EPOLLERR;
            }

            if (events == 0)
            {
                m_epollWatches.erase(fd);
                return;
            }

            watch.events = events;
            epoll_event ev {};
            ev.events  = events;
            ev.data.fd = fd;

            const int op = (prevEvents == 0) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
            if (::epoll_ctl(m_epollFd, op, fd, &ev) != 0 && op == EPOLL_CTL_ADD)
            {
                m_epollWatches.erase(fd);
            }
        }

        void UpdateEpollOnUnregisterLocked(Waiter* waiter) noexcept
        {
            if (m_epollFd < 0 || !waiter || !waiter->handle)
            {
                return;
            }

            const int fd = detail::ToNative(*waiter->handle);
            if (fd == detail::InvalidNativeSocket)
            {
                return;
            }

            auto it = m_epollWatches.find(fd);
            if (it == m_epollWatches.end())
            {
                return;
            }

            auto& watch = it->second;
            if (waiter->wantRead && watch.readers > 0)
            {
                --watch.readers;
            }
            if (waiter->wantWrite && watch.writers > 0)
            {
                --watch.writers;
            }

            std::uint32_t events = 0;
            if (watch.readers > 0)
            {
                events |= EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
            }
            if (watch.writers > 0)
            {
                events |= EPOLLOUT | EPOLLERR;
            }

            if (events == 0)
            {
                ::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
                m_epollWatches.erase(it);
                return;
            }

            watch.events = events;
            epoll_event ev {};
            ev.events  = events;
            ev.data.fd = fd;
            ::epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &ev);
        }
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
        void UpdateKqueueOnRegisterLocked(Waiter* waiter) noexcept
        {
            if (m_kqueueFd < 0 || !waiter || !waiter->handle)
            {
                return;
            }

            const int fd = detail::ToNative(*waiter->handle);
            if (fd == detail::InvalidNativeSocket)
            {
                return;
            }

            auto& watch        = m_kqueueWatches[fd];
            const int prevRead = watch.readers;
            const int prevWrite = watch.writers;

            if (waiter->wantRead)
            {
                ++watch.readers;
            }
            if (waiter->wantWrite)
            {
                ++watch.writers;
            }

            if (prevRead == 0 && watch.readers > 0)
            {
                kevent ev {};
                EV_SET(&ev,
                       static_cast<uintptr_t>(fd),
                       EVFILT_READ,
                       EV_ADD | EV_ENABLE,
                       0,
                       0,
                       nullptr);
                ::kevent(m_kqueueFd, &ev, 1, nullptr, 0, nullptr);
            }

            if (prevWrite == 0 && watch.writers > 0)
            {
                kevent ev {};
                EV_SET(&ev,
                       static_cast<uintptr_t>(fd),
                       EVFILT_WRITE,
                       EV_ADD | EV_ENABLE,
                       0,
                       0,
                       nullptr);
                ::kevent(m_kqueueFd, &ev, 1, nullptr, 0, nullptr);
            }
        }

        void UpdateKqueueOnUnregisterLocked(Waiter* waiter) noexcept
        {
            if (m_kqueueFd < 0 || !waiter || !waiter->handle)
            {
                return;
            }

            const int fd = detail::ToNative(*waiter->handle);
            if (fd == detail::InvalidNativeSocket)
            {
                return;
            }

            auto it = m_kqueueWatches.find(fd);
            if (it == m_kqueueWatches.end())
            {
                return;
            }

            auto& watch        = it->second;
            const int prevRead = watch.readers;
            const int prevWrite = watch.writers;

            if (waiter->wantRead && watch.readers > 0)
            {
                --watch.readers;
            }
            if (waiter->wantWrite && watch.writers > 0)
            {
                --watch.writers;
            }

            if (prevRead > 0 && watch.readers == 0)
            {
                kevent ev {};
                EV_SET(&ev,
                       static_cast<uintptr_t>(fd),
                       EVFILT_READ,
                       EV_DELETE,
                       0,
                       0,
                       nullptr);
                ::kevent(m_kqueueFd, &ev, 1, nullptr, 0, nullptr);
            }

            if (prevWrite > 0 && watch.writers == 0)
            {
                kevent ev {};
                EV_SET(&ev,
                       static_cast<uintptr_t>(fd),
                       EVFILT_WRITE,
                       EV_DELETE,
                       0,
                       0,
                       nullptr);
                ::kevent(m_kqueueFd, &ev, 1, nullptr, 0, nullptr);
            }

            if (watch.readers == 0 && watch.writers == 0)
            {
                m_kqueueWatches.erase(it);
            }
        }
#endif
        struct WaiterAwaiter final
        {
            Impl*                          owner {nullptr};
            SocketHandle*                  handle {nullptr};
            bool                           wantRead {false};
            bool                           wantWrite {false};
            NGIN::Execution::ExecutorRef   exec {};
            NGIN::Async::CancellationToken token {};
            Waiter                         waiter {};

            bool await_ready() const noexcept
            {
                if (owner == nullptr || token.IsCancellationRequested())
                {
                    return true;
                }
                if (!handle)
                {
                    return true;
                }
                return detail::ToNative(*handle) == detail::InvalidNativeSocket;
            }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                if (!owner)
                {
                    if (exec.IsValid())
                    {
                        exec.Execute(continuation);
                    }
                    else
                    {
                        continuation.resume();
                    }
                    return;
                }

                waiter.owner        = owner;
                waiter.handle       = handle;
                waiter.wantRead     = wantRead;
                waiter.wantWrite    = wantWrite;
                waiter.exec         = exec;
                waiter.continuation = continuation;

                owner->RegisterWaiter(&waiter);

                token.Register(waiter.cancellation, exec, continuation, +[](void* ctx) noexcept -> bool {
                                   auto* w = static_cast<Waiter*>(ctx);
                                   bool expected = false;
                                   if (!w->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                                   {
                                       return false;
                                   }
                                   if (w->owner)
                                   {
                                       w->owner->UnregisterWaiter(w);
                                   }
                                   return true; }, &waiter);
            }

            NGIN::Async::AsyncExpected<void> await_resume() noexcept
            {
                if (token.IsCancellationRequested())
                {
                    return std::unexpected(MakeCanceledError());
                }
                return {};
            }
        };

#if defined(NGIN_PLATFORM_WINDOWS)
        struct SendAwaiter final
        {
            Impl*                          owner {nullptr};
            SocketHandle*                  handle {nullptr};
            ConstByteSpan                  data {};
            NGIN::Execution::ExecutorRef   exec {};
            NGIN::Async::CancellationToken token {};
            IocpOperation                  op {};

            bool await_ready() const noexcept
            {
                return owner == nullptr || token.IsCancellationRequested();
            }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                if (!owner || !handle)
                {
                    if (exec.IsValid())
                    {
                        exec.Execute(continuation);
                    }
                    else
                    {
                        continuation.resume();
                    }
                    return;
                }

                op.cancellation.Reset();
                op.handle       = handle;
                op.exec         = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error         = NetError {NetErrorCode::Ok, 0};
                op.bytes         = 0;
                op.flags         = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                if (data.size() > std::numeric_limits<ULONG>::max())
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::MessageTooLarge, 0});
                    return;
                }

                op.buffer.buf = reinterpret_cast<char*>(const_cast<NGIN::Byte*>(data.data()));
                op.buffer.len = static_cast<ULONG>(data.size());

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD      bytes  = 0;
                const auto sock   = detail::ToNative(*handle);
                const int  result = ::WSASend(sock,
                                              &op.buffer,
                                              1,
                                              &bytes,
                                              op.flags,
                                              reinterpret_cast<LPWSAOVERLAPPED>(&op.overlapped),
                                              nullptr);
                if (result == 0)
                {
                    if (op.skipCompletionOnSuccess)
                    {
                        owner->CompleteOperation(op, bytes, 0);
                    }
                    return;
                }

                const int err = ::WSAGetLastError();
                if (err != WSA_IO_PENDING)
                {
                    owner->CompleteOperation(op, 0, static_cast<DWORD>(err));
                }
            }

            NGIN::Async::AsyncExpected<NGIN::UInt32> await_resume() noexcept
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    return std::unexpected(MakeCanceledError());
                }
                if (op.error.code != NetErrorCode::Ok)
                {
                    return std::unexpected(ToAsyncError(op.error));
                }
                return static_cast<NGIN::UInt32>(op.bytes);
            }
        };

        struct ReceiveAwaiter final
        {
            Impl*                          owner {nullptr};
            SocketHandle*                  handle {nullptr};
            ByteSpan                       destination {};
            NGIN::Execution::ExecutorRef   exec {};
            NGIN::Async::CancellationToken token {};
            IocpOperation                  op {};

            bool await_ready() const noexcept
            {
                return owner == nullptr || token.IsCancellationRequested();
            }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                if (!owner || !handle)
                {
                    if (exec.IsValid())
                    {
                        exec.Execute(continuation);
                    }
                    else
                    {
                        continuation.resume();
                    }
                    return;
                }

                op.cancellation.Reset();
                op.handle       = handle;
                op.exec         = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error         = NetError {NetErrorCode::Ok, 0};
                op.bytes         = 0;
                op.flags         = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                if (destination.size() > std::numeric_limits<ULONG>::max())
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::MessageTooLarge, 0});
                    return;
                }

                op.buffer.buf = reinterpret_cast<char*>(destination.data());
                op.buffer.len = static_cast<ULONG>(destination.size());

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD      bytes  = 0;
                DWORD      flags  = 0;
                const auto sock   = detail::ToNative(*handle);
                const int  result = ::WSARecv(sock,
                                              &op.buffer,
                                              1,
                                              &bytes,
                                              &flags,
                                              reinterpret_cast<LPWSAOVERLAPPED>(&op.overlapped),
                                              nullptr);
                op.flags          = flags;
                if (result == 0)
                {
                    if (op.skipCompletionOnSuccess)
                    {
                        owner->CompleteOperation(op, bytes, 0);
                    }
                    return;
                }

                const int err = ::WSAGetLastError();
                if (err != WSA_IO_PENDING)
                {
                    owner->CompleteOperation(op, 0, static_cast<DWORD>(err));
                }
            }

            NGIN::Async::AsyncExpected<NGIN::UInt32> await_resume() noexcept
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    return std::unexpected(MakeCanceledError());
                }
                if (op.error.code != NetErrorCode::Ok)
                {
                    return std::unexpected(ToAsyncError(op.error));
                }
                return static_cast<NGIN::UInt32>(op.bytes);
            }
        };

        struct SendToAwaiter final
        {
            Impl*                          owner {nullptr};
            SocketHandle*                  handle {nullptr};
            Endpoint                       remoteEndpoint {};
            ConstByteSpan                  data {};
            NGIN::Execution::ExecutorRef   exec {};
            NGIN::Async::CancellationToken token {};
            IocpOperation                  op {};

            bool await_ready() const noexcept
            {
                return owner == nullptr || token.IsCancellationRequested();
            }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                if (!owner || !handle)
                {
                    if (exec.IsValid())
                    {
                        exec.Execute(continuation);
                    }
                    else
                    {
                        continuation.resume();
                    }
                    return;
                }

                op.cancellation.Reset();
                op.handle       = handle;
                op.exec         = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error         = NetError {NetErrorCode::Ok, 0};
                op.bytes         = 0;
                op.flags         = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));
                std::memset(&op.address, 0, sizeof(op.address));

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                if (data.size() > std::numeric_limits<ULONG>::max())
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::MessageTooLarge, 0});
                    return;
                }

                socklen_t length = 0;
                if (!detail::ToSockAddr(remoteEndpoint, op.address, length))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, 0});
                    return;
                }
                op.addressLength = static_cast<int>(length);

                op.buffer.buf = reinterpret_cast<char*>(const_cast<NGIN::Byte*>(data.data()));
                op.buffer.len = static_cast<ULONG>(data.size());

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD      bytes  = 0;
                const auto sock   = detail::ToNative(*handle);
                const int  result = ::WSASendTo(sock,
                                                &op.buffer,
                                                1,
                                                &bytes,
                                                op.flags,
                                                reinterpret_cast<const sockaddr*>(&op.address),
                                                op.addressLength,
                                                reinterpret_cast<LPWSAOVERLAPPED>(&op.overlapped),
                                                nullptr);
                if (result == 0)
                {
                    if (op.skipCompletionOnSuccess)
                    {
                        owner->CompleteOperation(op, bytes, 0);
                    }
                    return;
                }

                const int err = ::WSAGetLastError();
                if (err != WSA_IO_PENDING)
                {
                    owner->CompleteOperation(op, 0, static_cast<DWORD>(err));
                }
            }

            NGIN::Async::AsyncExpected<NGIN::UInt32> await_resume() noexcept
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    return std::unexpected(MakeCanceledError());
                }
                if (op.error.code != NetErrorCode::Ok)
                {
                    return std::unexpected(ToAsyncError(op.error));
                }
                if (op.bytes == 0 && op.buffer.len > 0)
                {
                    return static_cast<NGIN::UInt32>(op.buffer.len);
                }
                return static_cast<NGIN::UInt32>(op.bytes);
            }
        };

        struct ReceiveFromAwaiter final
        {
            Impl*                          owner {nullptr};
            SocketHandle*                  handle {nullptr};
            ByteSpan                       destination {};
            NGIN::Execution::ExecutorRef   exec {};
            NGIN::Async::CancellationToken token {};
            IocpOperation                  op {};

            bool await_ready() const noexcept
            {
                return owner == nullptr || token.IsCancellationRequested();
            }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                if (!owner || !handle)
                {
                    if (exec.IsValid())
                    {
                        exec.Execute(continuation);
                    }
                    else
                    {
                        continuation.resume();
                    }
                    return;
                }

                op.cancellation.Reset();
                op.handle       = handle;
                op.exec         = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error         = NetError {NetErrorCode::Ok, 0};
                op.bytes         = 0;
                op.flags         = 0;
                op.addressLength = static_cast<int>(sizeof(op.address));
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));
                std::memset(&op.address, 0, sizeof(op.address));

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                if (destination.size() > std::numeric_limits<ULONG>::max())
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::MessageTooLarge, 0});
                    return;
                }

                op.buffer.buf = reinterpret_cast<char*>(destination.data());
                op.buffer.len = static_cast<ULONG>(destination.size());

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD      bytes  = 0;
                DWORD      flags  = 0;
                const auto sock   = detail::ToNative(*handle);
                const int  result = ::WSARecvFrom(sock,
                                                  &op.buffer,
                                                  1,
                                                  &bytes,
                                                  &flags,
                                                  reinterpret_cast<sockaddr*>(&op.address),
                                                  &op.addressLength,
                                                  reinterpret_cast<LPWSAOVERLAPPED>(&op.overlapped),
                                                  nullptr);
                op.flags          = flags;
                if (result == 0)
                {
                    if (op.skipCompletionOnSuccess)
                    {
                        owner->CompleteOperation(op, bytes, 0);
                    }
                    return;
                }

                const int err = ::WSAGetLastError();
                if (err != WSA_IO_PENDING)
                {
                    owner->CompleteOperation(op, 0, static_cast<DWORD>(err));
                }
            }

            NGIN::Async::AsyncExpected<DatagramReceiveResult> await_resume() noexcept
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    return std::unexpected(MakeCanceledError());
                }
                if (op.error.code != NetErrorCode::Ok)
                {
                    return std::unexpected(ToAsyncError(op.error));
                }

                DatagramReceiveResult result {};
                result.bytesReceived  = static_cast<NGIN::UInt32>(op.bytes);
                result.remoteEndpoint = detail::FromSockAddr(op.address, static_cast<socklen_t>(op.addressLength));
                return result;
            }
        };

        struct ConnectAwaiter final
        {
            Impl*                          owner {nullptr};
            SocketHandle*                  handle {nullptr};
            Endpoint                       remoteEndpoint {};
            NGIN::Execution::ExecutorRef   exec {};
            NGIN::Async::CancellationToken token {};
            IocpOperation                  op {};

            bool await_ready() const noexcept
            {
                return owner == nullptr || token.IsCancellationRequested();
            }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                if (!owner || !handle)
                {
                    if (exec.IsValid())
                    {
                        exec.Execute(continuation);
                    }
                    else
                    {
                        continuation.resume();
                    }
                    return;
                }

                op.cancellation.Reset();
                op.handle       = handle;
                op.exec         = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error         = NetError {NetErrorCode::Ok, 0};
                op.bytes         = 0;
                op.flags         = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));
                std::memset(&op.address, 0, sizeof(op.address));

                auto connectEx = detail::GetConnectEx();
                if (!connectEx)
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, 0});
                    return;
                }

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                socklen_t length = 0;
                if (!detail::ToSockAddr(remoteEndpoint, op.address, length))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, 0});
                    return;
                }
                op.addressLength = static_cast<int>(length);

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                const auto sock   = detail::ToNative(*handle);
                const BOOL result = connectEx(sock,
                                              reinterpret_cast<sockaddr*>(&op.address),
                                              op.addressLength,
                                              nullptr,
                                              0,
                                              nullptr,
                                              reinterpret_cast<LPWSAOVERLAPPED>(&op.overlapped));
                if (result != FALSE)
                {
                    if (op.skipCompletionOnSuccess)
                    {
                        owner->CompleteOperation(op, 0, 0);
                    }
                    return;
                }

                const int err = ::WSAGetLastError();
                if (err != WSA_IO_PENDING)
                {
                    owner->CompleteOperation(op, 0, static_cast<DWORD>(err));
                }
            }

            NGIN::Async::AsyncExpected<void> await_resume() noexcept
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    return std::unexpected(MakeCanceledError());
                }
                if (op.error.code != NetErrorCode::Ok)
                {
                    return std::unexpected(ToAsyncError(op.error));
                }

                const auto sock   = detail::ToNative(*handle);
                const int  result = ::setsockopt(sock, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
                if (result != 0)
                {
                    return std::unexpected(ToAsyncError(detail::LastError()));
                }
                return {};
            }
        };

        struct AcceptAwaiter final
        {
            static constexpr std::size_t AddressBytes = sizeof(sockaddr_storage) + 16;
            static constexpr std::size_t BufferBytes  = AddressBytes * 2;

            Impl*                               owner {nullptr};
            SocketHandle*                       listenHandle {nullptr};
            NGIN::Execution::ExecutorRef        exec {};
            NGIN::Async::CancellationToken      token {};
            IocpOperation                       op {};
            SocketHandle                        accepted {};
            std::array<NGIN::Byte, BufferBytes> buffer {};

            bool await_ready() const noexcept
            {
                return owner == nullptr || token.IsCancellationRequested();
            }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                if (!owner || !listenHandle)
                {
                    if (exec.IsValid())
                    {
                        exec.Execute(continuation);
                    }
                    else
                    {
                        continuation.resume();
                    }
                    return;
                }

                op.cancellation.Reset();
                op.handle       = listenHandle;
                op.exec         = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error         = NetError {NetErrorCode::Ok, 0};
                op.bytes         = 0;
                op.flags         = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));

                auto acceptEx = detail::GetAcceptEx();
                if (!acceptEx)
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, 0});
                    return;
                }

                const AddressFamily family = detail::GetSocketFamily(*listenHandle);
                NetError createError {};
                accepted = detail::CreateSocket(family, SOCK_STREAM, IPPROTO_TCP, true, createError);
                if (createError.code != NetErrorCode::Ok)
                {
                    owner->CompleteOperationWithError(op, 0, createError);
                    return;
                }

                if (!owner->EnsureAssociated(*listenHandle))
                {
                    accepted.Close();
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrorCode::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*listenHandle);

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD       bytes        = 0;
                const auto  listenSock   = detail::ToNative(*listenHandle);
                const auto  acceptSock   = detail::ToNative(accepted);
                const DWORD addressBytes = static_cast<DWORD>(AddressBytes);
                const BOOL  result       = acceptEx(listenSock,
                                                    acceptSock,
                                                    buffer.data(),
                                                    0,
                                                    addressBytes,
                                                    addressBytes,
                                                    &bytes,
                                                    reinterpret_cast<LPWSAOVERLAPPED>(&op.overlapped));
                if (result != FALSE)
                {
                    if (op.skipCompletionOnSuccess)
                    {
                        owner->CompleteOperation(op, bytes, 0);
                    }
                    return;
                }

                const int err = ::WSAGetLastError();
                if (err != WSA_IO_PENDING)
                {
                    accepted.Close();
                    owner->CompleteOperation(op, 0, static_cast<DWORD>(err));
                }
            }

            NGIN::Async::AsyncExpected<SocketHandle> await_resume() noexcept
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    accepted.Close();
                    return std::unexpected(MakeCanceledError());
                }
                if (op.error.code != NetErrorCode::Ok)
                {
                    accepted.Close();
                    return std::unexpected(ToAsyncError(op.error));
                }

                const auto listenSock = detail::ToNative(*listenHandle);
                const auto acceptSock = detail::ToNative(accepted);
                const int  update     = ::setsockopt(acceptSock,
                                                     SOL_SOCKET,
                                                     SO_UPDATE_ACCEPT_CONTEXT,
                                                     reinterpret_cast<const char*>(&listenSock),
                                                     sizeof(listenSock));
                if (update != 0)
                {
                    accepted.Close();
                    return std::unexpected(ToAsyncError(detail::LastError()));
                }

                if (!owner->EnsureAssociated(accepted))
                {
                    accepted.Close();
                    return std::unexpected(ToAsyncError(detail::LastError()));
                }

                return std::move(accepted);
            }
        };
#endif

        void StartWorkers()
        {
            if (!m_workers.empty())
            {
                return;
            }

            const int timeoutMs = GetPollTimeoutMs();
            m_workers.reserve(m_options.workerThreads);
            for (NGIN::UInt32 i = 0; i < m_options.workerThreads; ++i)
            {
                NGIN::Execution::Thread::Options options {};
                options.name = MakeIndexedThreadName("NGIN.NetW", i);
                m_workers.emplace_back([this, timeoutMs]() {
                    while (!m_stop.load(std::memory_order_acquire))
                    {
                        PollOnce(timeoutMs);
                    }
                },
                                       options);
            }
        }

        void JoinWorkers() noexcept
        {
            for (auto& worker: m_workers)
            {
                if (worker.IsJoinable())
                {
                    worker.Join();
                }
            }
        }

        static NGIN::Execution::ThreadName MakeIndexedThreadName(std::string_view prefix, std::size_t index) noexcept
        {
            std::array<char, NGIN::Execution::ThreadName::MaxBytes + 1> buffer {};
            const auto                                                  prefixLen = std::min<std::size_t>(prefix.size(), NGIN::Execution::ThreadName::MaxBytes);
            for (std::size_t i = 0; i < prefixLen; ++i)
            {
                buffer[i] = prefix[i];
            }

            std::size_t pos = prefixLen;
            if (pos < NGIN::Execution::ThreadName::MaxBytes)
            {
                buffer[pos++] = '.';
            }

            std::array<char, 24> digits {};
            std::size_t          digitCount = 0;
            auto                 value      = index;
            do
            {
                digits[digitCount++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            } while (value != 0 && digitCount < digits.size());

            while (digitCount > 0 && pos < NGIN::Execution::ThreadName::MaxBytes)
            {
                buffer[pos++] = digits[--digitCount];
            }
            buffer[pos] = '\0';

            return NGIN::Execution::ThreadName(std::string_view(buffer.data(), pos));
        }

        int GetPollTimeoutMs() const noexcept
        {
            if (m_options.busyPoll)
            {
                return 0;
            }

            const auto value = m_options.pollInterval.GetValue();
            if (value <= 0.0)
            {
                return 0;
            }

            const auto maxMs = static_cast<double>(std::numeric_limits<int>::max());
            if (value > maxMs)
            {
                return std::numeric_limits<int>::max();
            }

            return static_cast<int>(value);
        }

    public:
        static NGIN::Async::Task<void> WaitUntilReadable(NGIN::Async::TaskContext&      ctx,
                                                         Impl&                          owner,
                                                         SocketHandle&                  handle,
                                                         NGIN::Async::CancellationToken token)
        {
            WaiterAwaiter awaiter {};
            awaiter.owner    = &owner;
            awaiter.handle   = &handle;
            awaiter.wantRead = true;
            awaiter.exec     = ctx.GetExecutor();
            awaiter.token    = token;
            auto result = co_await awaiter;
            if (!result)
            {
                co_await NGIN::Async::Task<void>::ReturnError(result.error());
                co_return;
            }
            co_return;
        }

        static NGIN::Async::Task<void> WaitUntilWritable(NGIN::Async::TaskContext&      ctx,
                                                         Impl&                          owner,
                                                         SocketHandle&                  handle,
                                                         NGIN::Async::CancellationToken token)
        {
            WaiterAwaiter awaiter {};
            awaiter.owner     = &owner;
            awaiter.handle    = &handle;
            awaiter.wantWrite = true;
            awaiter.exec      = ctx.GetExecutor();
            awaiter.token     = token;
            auto result = co_await awaiter;
            if (!result)
            {
                co_await NGIN::Async::Task<void>::ReturnError(result.error());
                co_return;
            }
            co_return;
        }

#if defined(NGIN_PLATFORM_WINDOWS)
        static NGIN::Async::Task<NGIN::UInt32> SubmitSend(NGIN::Async::TaskContext&      ctx,
                                                          Impl&                          owner,
                                                          SocketHandle&                  handle,
                                                          ConstByteSpan                  data,
                                                          NGIN::Async::CancellationToken token)
        {
            SendAwaiter awaiter {};
            awaiter.owner  = &owner;
            awaiter.handle = &handle;
            awaiter.data   = data;
            awaiter.exec   = ctx.GetExecutor();
            awaiter.token  = token;
            co_return co_await awaiter;
        }

        static NGIN::Async::Task<NGIN::UInt32> SubmitReceive(NGIN::Async::TaskContext&      ctx,
                                                             Impl&                          owner,
                                                             SocketHandle&                  handle,
                                                             ByteSpan                       destination,
                                                             NGIN::Async::CancellationToken token)
        {
            ReceiveAwaiter awaiter {};
            awaiter.owner       = &owner;
            awaiter.handle      = &handle;
            awaiter.destination = destination;
            awaiter.exec        = ctx.GetExecutor();
            awaiter.token       = token;
            co_return co_await awaiter;
        }

        static NGIN::Async::Task<NGIN::UInt32> SubmitSendTo(NGIN::Async::TaskContext&      ctx,
                                                            Impl&                          owner,
                                                            SocketHandle&                  handle,
                                                            Endpoint                       remoteEndpoint,
                                                            ConstByteSpan                  data,
                                                            NGIN::Async::CancellationToken token)
        {
            SendToAwaiter awaiter {};
            awaiter.owner          = &owner;
            awaiter.handle         = &handle;
            awaiter.remoteEndpoint = remoteEndpoint;
            awaiter.data           = data;
            awaiter.exec           = ctx.GetExecutor();
            awaiter.token          = token;
            co_return co_await awaiter;
        }

        static NGIN::Async::Task<DatagramReceiveResult> SubmitReceiveFrom(NGIN::Async::TaskContext&      ctx,
                                                                          Impl&                          owner,
                                                                          SocketHandle&                  handle,
                                                                          ByteSpan                       destination,
                                                                          NGIN::Async::CancellationToken token)
        {
            ReceiveFromAwaiter awaiter {};
            awaiter.owner       = &owner;
            awaiter.handle      = &handle;
            awaiter.destination = destination;
            awaiter.exec        = ctx.GetExecutor();
            awaiter.token       = token;
            co_return co_await awaiter;
        }

        static NGIN::Async::Task<void> SubmitConnect(NGIN::Async::TaskContext&      ctx,
                                                     Impl&                          owner,
                                                     SocketHandle&                  handle,
                                                     Endpoint                       remoteEndpoint,
                                                     NGIN::Async::CancellationToken token)
        {
            ConnectAwaiter awaiter {};
            awaiter.owner          = &owner;
            awaiter.handle         = &handle;
            awaiter.remoteEndpoint = remoteEndpoint;
            awaiter.exec           = ctx.GetExecutor();
            awaiter.token          = token;
            auto result = co_await awaiter;
            if (!result)
            {
                co_await NGIN::Async::Task<void>::ReturnError(result.error());
                co_return;
            }
            co_return;
        }

        static NGIN::Async::Task<SocketHandle> SubmitAccept(NGIN::Async::TaskContext&      ctx,
                                                            Impl&                          owner,
                                                            SocketHandle&                  handle,
                                                            NGIN::Async::CancellationToken token)
        {
            AcceptAwaiter awaiter {};
            awaiter.owner        = &owner;
            awaiter.listenHandle = &handle;
            awaiter.exec         = ctx.GetExecutor();
            awaiter.token        = token;
            co_return co_await awaiter;
        }
#endif
    };

    NetworkDriver::NetworkDriver()
        : m_impl(std::make_unique<Impl>(NetworkDriverOptions {}))
    {
    }

    NetworkDriver::~NetworkDriver()
    {
        if (m_impl)
        {
            m_impl->Shutdown();
        }
    }

    std::unique_ptr<NetworkDriver> NetworkDriver::Create(NetworkDriverOptions options)
    {
        auto driver    = std::unique_ptr<NetworkDriver>(new NetworkDriver());
        driver->m_impl = std::make_unique<Impl>(options);
        return driver;
    }

    void NetworkDriver::Run()
    {
        if (m_impl)
        {
            m_impl->Run();
        }
    }

    void NetworkDriver::PollOnce()
    {
        if (m_impl)
        {
            m_impl->PollOnce(0);
        }
    }

    void NetworkDriver::Stop()
    {
        if (m_impl)
        {
            m_impl->Stop();
        }
    }

    NGIN::Async::Task<void> NetworkDriver::WaitUntilReadable(NGIN::Async::TaskContext&      ctx,
                                                             SocketHandle&                  handle,
                                                             NGIN::Async::CancellationToken token)
    {
        return Impl::WaitUntilReadable(ctx, *m_impl, handle, token);
    }

    NGIN::Async::Task<void> NetworkDriver::WaitUntilWritable(NGIN::Async::TaskContext&      ctx,
                                                             SocketHandle&                  handle,
                                                             NGIN::Async::CancellationToken token)
    {
        return Impl::WaitUntilWritable(ctx, *m_impl, handle, token);
    }

#if defined(NGIN_PLATFORM_WINDOWS)
    NGIN::Async::Task<NGIN::UInt32> NetworkDriver::SubmitSend(NGIN::Async::TaskContext&      ctx,
                                                              SocketHandle&                  handle,
                                                              ConstByteSpan                  data,
                                                              NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitSend(ctx, *m_impl, handle, data, token);
    }

    NGIN::Async::Task<NGIN::UInt32> NetworkDriver::SubmitReceive(NGIN::Async::TaskContext&      ctx,
                                                                 SocketHandle&                  handle,
                                                                 ByteSpan                       destination,
                                                                 NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitReceive(ctx, *m_impl, handle, destination, token);
    }

    NGIN::Async::Task<NGIN::UInt32> NetworkDriver::SubmitSendTo(NGIN::Async::TaskContext&      ctx,
                                                                SocketHandle&                  handle,
                                                                Endpoint                       remoteEndpoint,
                                                                ConstByteSpan                  data,
                                                                NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitSendTo(ctx, *m_impl, handle, remoteEndpoint, data, token);
    }

    NGIN::Async::Task<DatagramReceiveResult> NetworkDriver::SubmitReceiveFrom(NGIN::Async::TaskContext&      ctx,
                                                                              SocketHandle&                  handle,
                                                                              ByteSpan                       destination,
                                                                              NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitReceiveFrom(ctx, *m_impl, handle, destination, token);
    }

    NGIN::Async::Task<void> NetworkDriver::SubmitConnect(NGIN::Async::TaskContext&      ctx,
                                                         SocketHandle&                  handle,
                                                         Endpoint                       remoteEndpoint,
                                                         NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitConnect(ctx, *m_impl, handle, remoteEndpoint, token);
    }

    NGIN::Async::Task<SocketHandle> NetworkDriver::SubmitAccept(NGIN::Async::TaskContext&      ctx,
                                                                SocketHandle&                  handle,
                                                                NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitAccept(ctx, *m_impl, handle, token);
    }
#endif
}// namespace NGIN::Net


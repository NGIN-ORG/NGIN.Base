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
  #include <NGIN/Net/Sockets/UdpSocket.hpp>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::Net
{
    struct NetworkDriver::Impl final
    {
        struct Waiter final
        {
            Impl*                              owner {nullptr};
            SocketHandle*                      handle {nullptr};
            bool                               wantRead {false};
            bool                               wantWrite {false};
            NGIN::Execution::ExecutorRef       exec {};
            std::coroutine_handle<>            continuation {};
            NGIN::Async::CancellationRegistration cancellation {};
            std::atomic<bool>                  done {false};
        };

#if defined(NGIN_PLATFORM_WINDOWS)
        struct IocpOperation final
        {
            WSAOVERLAPPED                    overlapped {};
            WSABUF                           buffer {};
            SocketHandle*                    handle {nullptr};
            NGIN::Execution::ExecutorRef     exec {};
            std::coroutine_handle<>          continuation {};
            NGIN::Async::CancellationRegistration cancellation {};
            std::atomic<bool>                done {false};
            NetError                         error {};
            DWORD                            bytes {0};
            DWORD                            flags {0};
            sockaddr_storage                 address {};
            int                              addressLength {0};
            bool                             skipCompletionOnSuccess {false};
        };
#endif

        explicit Impl(NetworkDriverOptions options)
            : m_options(options)
        {
#if defined(NGIN_PLATFORM_WINDOWS)
            m_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
#endif
        }

#if defined(NGIN_PLATFORM_WINDOWS)
        ~Impl()
        {
            if (m_iocp)
            {
                ::CloseHandle(m_iocp);
                m_iocp = nullptr;
            }
        }
#endif

        void RegisterWaiter(Waiter* waiter)
        {
            std::lock_guard guard(m_mutex);
            m_waiters.push_back(waiter);
        }

        void UnregisterWaiter(Waiter* waiter)
        {
            std::lock_guard guard(m_mutex);
            auto it = std::find(m_waiters.begin(), m_waiters.end(), waiter);
            if (it != m_waiters.end())
            {
                *it = m_waiters.back();
                m_waiters.pop_back();
            }
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
            const BOOL ok = ::SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(sock), flags);
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
                op.error = NetError {NetErrc::Ok, 0};
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

        void PumpIocp() noexcept
        {
            if (!m_iocp)
            {
                return;
            }

            for (int i = 0; i < 64; ++i)
            {
                DWORD bytes = 0;
                ULONG_PTR key = 0;
                LPOVERLAPPED overlapped = nullptr;
                const BOOL ok = ::GetQueuedCompletionStatus(m_iocp, &bytes, &key, &overlapped, 0);
                if (!overlapped)
                {
                    break;
                }

                const DWORD error = ok ? 0 : ::GetLastError();
                auto* op = reinterpret_cast<IocpOperation*>(overlapped);
                if (op)
                {
                    CompleteOperation(*op, bytes, error);
                }
            }
        }
#endif

        void PollOnce()
        {
#if defined(NGIN_PLATFORM_WINDOWS)
            PumpIocp();
#endif
            std::vector<Waiter*> waiters;
            {
                std::lock_guard guard(m_mutex);
                if (m_waiters.empty())
                {
                    return;
                }
                waiters = m_waiters;
            }

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
            timeout.tv_sec = 0;
            timeout.tv_usec = 0;

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
                    continue;
                }
                const auto sock = detail::ToNative(*waiter->handle);
                if (sock == detail::InvalidNativeSocket)
                {
                    continue;
                }

                const bool readyRead = waiter->wantRead && FD_ISSET(sock, &readSet);
                const bool readyWrite = waiter->wantWrite && FD_ISSET(sock, &writeSet);
                if (!readyRead && !readyWrite)
                {
                    continue;
                }

                bool expected = false;
                if (!waiter->done.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    continue;
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
        }

        void Run()
        {
            if (m_options.workerThreads == 0)
            {
                while (!m_stop.load(std::memory_order_acquire))
                {
                    PollOnce();
                    if (!m_options.busyPoll)
                    {
                        NGIN::Execution::ThisThread::SleepFor(m_options.pollInterval);
                    }
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

        NetworkDriverOptions m_options {};
        std::mutex m_mutex {};
        std::vector<Waiter*> m_waiters {};
        std::atomic<bool> m_stop {false};
        std::vector<NGIN::Execution::Thread> m_workers {};
#if defined(NGIN_PLATFORM_WINDOWS)
        HANDLE m_iocp {nullptr};
#endif

    private:
        struct WaiterAwaiter final
        {
            Impl*                         owner {nullptr};
            SocketHandle*                 handle {nullptr};
            bool                          wantRead {false};
            bool                          wantWrite {false};
            NGIN::Execution::ExecutorRef  exec {};
            NGIN::Async::CancellationToken token {};
            Waiter                        waiter {};

            bool await_ready() const noexcept
            {
                return owner == nullptr || token.IsCancellationRequested();
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

                waiter.owner = owner;
                waiter.handle = handle;
                waiter.wantRead = wantRead;
                waiter.wantWrite = wantWrite;
                waiter.exec = exec;
                waiter.continuation = continuation;

                owner->RegisterWaiter(&waiter);

                token.Register(waiter.cancellation,
                               exec,
                               continuation,
                               +[](void* ctx) noexcept -> bool {
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
                                   return true;
                               },
                               &waiter);
            }

            void await_resume()
            {
                if (token.IsCancellationRequested())
                {
                    throw NGIN::Async::TaskCanceled();
                }
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
                op.handle = handle;
                op.exec = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error = NetError {NetErrc::Ok, 0};
                op.bytes = 0;
                op.flags = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                if (data.size() > std::numeric_limits<ULONG>::max())
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::MessageTooLarge, 0});
                    return;
                }

                op.buffer.buf = reinterpret_cast<char*>(const_cast<NGIN::Byte*>(data.data()));
                op.buffer.len = static_cast<ULONG>(data.size());

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD bytes = 0;
                const auto sock = detail::ToNative(*handle);
                const int result = ::WSASend(sock,
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

            NGIN::UInt32 await_resume()
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    throw NGIN::Async::TaskCanceled();
                }
                if (op.error.code != NetErrc::Ok)
                {
                    throw std::runtime_error("NetworkDriver IOCP send failed: " + std::to_string(op.error.native));
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
                op.handle = handle;
                op.exec = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error = NetError {NetErrc::Ok, 0};
                op.bytes = 0;
                op.flags = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                if (destination.size() > std::numeric_limits<ULONG>::max())
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::MessageTooLarge, 0});
                    return;
                }

                op.buffer.buf = reinterpret_cast<char*>(destination.data());
                op.buffer.len = static_cast<ULONG>(destination.size());

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD bytes = 0;
                DWORD flags = 0;
                const auto sock = detail::ToNative(*handle);
                const int result = ::WSARecv(sock,
                                             &op.buffer,
                                             1,
                                             &bytes,
                                             &flags,
                                             reinterpret_cast<LPWSAOVERLAPPED>(&op.overlapped),
                                             nullptr);
                op.flags = flags;
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

            NGIN::UInt32 await_resume()
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    throw NGIN::Async::TaskCanceled();
                }
                if (op.error.code != NetErrc::Ok)
                {
                    throw std::runtime_error("NetworkDriver IOCP receive failed");
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
                op.handle = handle;
                op.exec = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error = NetError {NetErrc::Ok, 0};
                op.bytes = 0;
                op.flags = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));
                std::memset(&op.address, 0, sizeof(op.address));

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                if (data.size() > std::numeric_limits<ULONG>::max())
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::MessageTooLarge, 0});
                    return;
                }

                socklen_t length = 0;
                if (!detail::ToSockAddr(remoteEndpoint, op.address, length))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, 0});
                    return;
                }
                op.addressLength = static_cast<int>(length);

                op.buffer.buf = reinterpret_cast<char*>(const_cast<NGIN::Byte*>(data.data()));
                op.buffer.len = static_cast<ULONG>(data.size());

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD bytes = 0;
                const auto sock = detail::ToNative(*handle);
                const int result = ::WSASendTo(sock,
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

            NGIN::UInt32 await_resume()
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    throw NGIN::Async::TaskCanceled();
                }
                if (op.error.code != NetErrc::Ok)
                {
                    throw std::runtime_error("NetworkDriver IOCP send-to failed");
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
                op.handle = handle;
                op.exec = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error = NetError {NetErrc::Ok, 0};
                op.bytes = 0;
                op.flags = 0;
                op.addressLength = static_cast<int>(sizeof(op.address));
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));
                std::memset(&op.address, 0, sizeof(op.address));

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                if (destination.size() > std::numeric_limits<ULONG>::max())
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::MessageTooLarge, 0});
                    return;
                }

                op.buffer.buf = reinterpret_cast<char*>(destination.data());
                op.buffer.len = static_cast<ULONG>(destination.size());

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD bytes = 0;
                DWORD flags = 0;
                const auto sock = detail::ToNative(*handle);
                const int result = ::WSARecvFrom(sock,
                                                 &op.buffer,
                                                 1,
                                                 &bytes,
                                                 &flags,
                                                 reinterpret_cast<sockaddr*>(&op.address),
                                                 &op.addressLength,
                                                 reinterpret_cast<LPWSAOVERLAPPED>(&op.overlapped),
                                                 nullptr);
                op.flags = flags;
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

            DatagramReceiveResult await_resume()
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    throw NGIN::Async::TaskCanceled();
                }
                if (op.error.code != NetErrc::Ok)
                {
                    throw std::runtime_error("NetworkDriver IOCP receive-from failed");
                }

                DatagramReceiveResult result {};
                result.bytesReceived = static_cast<NGIN::UInt32>(op.bytes);
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
                op.handle = handle;
                op.exec = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error = NetError {NetErrc::Ok, 0};
                op.bytes = 0;
                op.flags = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));
                std::memset(&op.address, 0, sizeof(op.address));

                auto connectEx = detail::GetConnectEx();
                if (!connectEx)
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, 0});
                    return;
                }

                if (!owner->EnsureAssociated(*handle))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*handle);

                socklen_t length = 0;
                if (!detail::ToSockAddr(remoteEndpoint, op.address, length))
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, 0});
                    return;
                }
                op.addressLength = static_cast<int>(length);

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                const auto sock = detail::ToNative(*handle);
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

            void await_resume()
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    throw NGIN::Async::TaskCanceled();
                }
                if (op.error.code != NetErrc::Ok)
                {
                    throw std::runtime_error("NetworkDriver IOCP connect failed");
                }

                const auto sock = detail::ToNative(*handle);
                const int result = ::setsockopt(sock, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
                if (result != 0)
                {
                    throw std::runtime_error("NetworkDriver IOCP connect context update failed");
                }
            }
        };

        struct AcceptAwaiter final
        {
            static constexpr std::size_t AddressBytes = sizeof(sockaddr_storage) + 16;
            static constexpr std::size_t BufferBytes = AddressBytes * 2;

            Impl*                          owner {nullptr};
            SocketHandle*                  listenHandle {nullptr};
            NGIN::Execution::ExecutorRef   exec {};
            NGIN::Async::CancellationToken token {};
            IocpOperation                  op {};
            SocketHandle                   accepted {};
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
                op.handle = listenHandle;
                op.exec = exec;
                op.continuation = continuation;
                op.done.store(false, std::memory_order_release);
                op.error = NetError {NetErrc::Ok, 0};
                op.bytes = 0;
                op.flags = 0;
                op.addressLength = 0;
                std::memset(&op.overlapped, 0, sizeof(op.overlapped));

                auto acceptEx = detail::GetAcceptEx();
                if (!acceptEx)
                {
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, 0});
                    return;
                }

                const AddressFamily family = detail::GetSocketFamily(*listenHandle);
                bool dualStack = false;
                if (family == AddressFamily::V6)
                {
                    dualStack = !detail::IsV6Only(*listenHandle);
                }
                NetError createError {};
                accepted = detail::CreateSocket(family, SOCK_STREAM, IPPROTO_TCP, dualStack, createError);
                if (createError.code != NetErrc::Ok)
                {
                    owner->CompleteOperationWithError(op, 0, createError);
                    return;
                }

                if (!owner->EnsureAssociated(*listenHandle))
                {
                    accepted.Close();
                    owner->CompleteOperationWithError(op, 0, NetError {NetErrc::Unknown, static_cast<int>(::GetLastError())});
                    return;
                }

                op.skipCompletionOnSuccess = owner->TrySkipCompletionOnSuccess(*listenHandle);

                token.Register(op.cancellation, exec, continuation, &CancelIocp, &op);

                DWORD bytes = 0;
                const auto listenSock = detail::ToNative(*listenHandle);
                const auto acceptSock = detail::ToNative(accepted);
                const DWORD addressBytes = static_cast<DWORD>(AddressBytes);
                const BOOL result = acceptEx(listenSock,
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

            SocketHandle await_resume()
            {
                if (token.IsCancellationRequested() || op.error.native == ERROR_OPERATION_ABORTED)
                {
                    accepted.Close();
                    throw NGIN::Async::TaskCanceled();
                }
                if (op.error.code != NetErrc::Ok)
                {
                    accepted.Close();
                    throw std::runtime_error("NetworkDriver IOCP accept failed");
                }

                const auto listenSock = detail::ToNative(*listenHandle);
                const auto acceptSock = detail::ToNative(accepted);
                const int update = ::setsockopt(acceptSock,
                                                SOL_SOCKET,
                                                SO_UPDATE_ACCEPT_CONTEXT,
                                                reinterpret_cast<const char*>(&listenSock),
                                                sizeof(listenSock));
                if (update != 0)
                {
                    accepted.Close();
                    throw std::runtime_error("NetworkDriver IOCP accept context update failed");
                }

                if (!owner->EnsureAssociated(accepted))
                {
                    accepted.Close();
                    throw std::runtime_error("NetworkDriver IOCP accept association failed");
                }

                return accepted;
            }
        };
#endif

        void StartWorkers()
        {
            if (!m_workers.empty())
            {
                return;
            }

            m_workers.reserve(m_options.workerThreads);
            for (NGIN::UInt32 i = 0; i < m_options.workerThreads; ++i)
            {
                NGIN::Execution::Thread::Options options {};
                options.name = MakeIndexedThreadName("NGIN.NetW", i);
                m_workers.emplace_back([this]() {
                    while (!m_stop.load(std::memory_order_acquire))
                    {
                        PollOnce();
                        if (!m_options.busyPoll)
                        {
                            NGIN::Execution::ThisThread::SleepFor(m_options.pollInterval);
                        }
                    }
                }, options);
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
            const auto prefixLen = std::min<std::size_t>(prefix.size(), NGIN::Execution::ThreadName::MaxBytes);
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
            std::size_t digitCount = 0;
            auto value = index;
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

    public:
        NGIN::Async::Task<void> WaitUntilReadable(NGIN::Async::TaskContext& ctx,
                                                  SocketHandle& handle,
                                                  NGIN::Async::CancellationToken token)
        {
            WaiterAwaiter awaiter {};
            awaiter.owner = this;
            awaiter.handle = &handle;
            awaiter.wantRead = true;
            awaiter.exec = ctx.GetExecutor();
            awaiter.token = token;
            co_await awaiter;
        }

        NGIN::Async::Task<void> WaitUntilWritable(NGIN::Async::TaskContext& ctx,
                                                  SocketHandle& handle,
                                                  NGIN::Async::CancellationToken token)
        {
            WaiterAwaiter awaiter {};
            awaiter.owner = this;
            awaiter.handle = &handle;
            awaiter.wantWrite = true;
            awaiter.exec = ctx.GetExecutor();
            awaiter.token = token;
            co_await awaiter;
        }

#if defined(NGIN_PLATFORM_WINDOWS)
        static NGIN::Async::Task<NGIN::UInt32> SubmitSend(NGIN::Async::TaskContext& ctx,
                                                          Impl& owner,
                                                          SocketHandle& handle,
                                                          ConstByteSpan data,
                                                          NGIN::Async::CancellationToken token)
        {
            SendAwaiter awaiter {};
            awaiter.owner = &owner;
            awaiter.handle = &handle;
            awaiter.data = data;
            awaiter.exec = ctx.GetExecutor();
            awaiter.token = token;
            co_return co_await awaiter;
        }

        static NGIN::Async::Task<NGIN::UInt32> SubmitReceive(NGIN::Async::TaskContext& ctx,
                                                             Impl& owner,
                                                             SocketHandle& handle,
                                                             ByteSpan destination,
                                                             NGIN::Async::CancellationToken token)
        {
            ReceiveAwaiter awaiter {};
            awaiter.owner = &owner;
            awaiter.handle = &handle;
            awaiter.destination = destination;
            awaiter.exec = ctx.GetExecutor();
            awaiter.token = token;
            co_return co_await awaiter;
        }

        static NGIN::Async::Task<NGIN::UInt32> SubmitSendTo(NGIN::Async::TaskContext& ctx,
                                                            Impl& owner,
                                                            SocketHandle& handle,
                                                            Endpoint remoteEndpoint,
                                                            ConstByteSpan data,
                                                            NGIN::Async::CancellationToken token)
        {
            SendToAwaiter awaiter {};
            awaiter.owner = &owner;
            awaiter.handle = &handle;
            awaiter.remoteEndpoint = remoteEndpoint;
            awaiter.data = data;
            awaiter.exec = ctx.GetExecutor();
            awaiter.token = token;
            co_return co_await awaiter;
        }

        static NGIN::Async::Task<DatagramReceiveResult> SubmitReceiveFrom(NGIN::Async::TaskContext& ctx,
                                                                          Impl& owner,
                                                                          SocketHandle& handle,
                                                                          ByteSpan destination,
                                                                          NGIN::Async::CancellationToken token)
        {
            ReceiveFromAwaiter awaiter {};
            awaiter.owner = &owner;
            awaiter.handle = &handle;
            awaiter.destination = destination;
            awaiter.exec = ctx.GetExecutor();
            awaiter.token = token;
            co_return co_await awaiter;
        }

        static NGIN::Async::Task<void> SubmitConnect(NGIN::Async::TaskContext& ctx,
                                                     Impl& owner,
                                                     SocketHandle& handle,
                                                     Endpoint remoteEndpoint,
                                                     NGIN::Async::CancellationToken token)
        {
            ConnectAwaiter awaiter {};
            awaiter.owner = &owner;
            awaiter.handle = &handle;
            awaiter.remoteEndpoint = remoteEndpoint;
            awaiter.exec = ctx.GetExecutor();
            awaiter.token = token;
            co_await awaiter;
        }

        static NGIN::Async::Task<SocketHandle> SubmitAccept(NGIN::Async::TaskContext& ctx,
                                                            Impl& owner,
                                                            SocketHandle& handle,
                                                            NGIN::Async::CancellationToken token)
        {
            AcceptAwaiter awaiter {};
            awaiter.owner = &owner;
            awaiter.listenHandle = &handle;
            awaiter.exec = ctx.GetExecutor();
            awaiter.token = token;
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
        auto driver = std::unique_ptr<NetworkDriver>(new NetworkDriver());
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
            m_impl->PollOnce();
        }
    }

    void NetworkDriver::Stop()
    {
        if (m_impl)
        {
            m_impl->Stop();
        }
    }

    NGIN::Async::Task<void> NetworkDriver::WaitUntilReadable(NGIN::Async::TaskContext& ctx,
                                                             SocketHandle& handle,
                                                             NGIN::Async::CancellationToken token)
    {
        return m_impl->WaitUntilReadable(ctx, handle, token);
    }

    NGIN::Async::Task<void> NetworkDriver::WaitUntilWritable(NGIN::Async::TaskContext& ctx,
                                                             SocketHandle& handle,
                                                             NGIN::Async::CancellationToken token)
    {
        return m_impl->WaitUntilWritable(ctx, handle, token);
    }

#if defined(NGIN_PLATFORM_WINDOWS)
    NGIN::Async::Task<NGIN::UInt32> NetworkDriver::SubmitSend(NGIN::Async::TaskContext& ctx,
                                                              SocketHandle& handle,
                                                              ConstByteSpan data,
                                                              NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitSend(ctx, *m_impl, handle, data, token);
    }

    NGIN::Async::Task<NGIN::UInt32> NetworkDriver::SubmitReceive(NGIN::Async::TaskContext& ctx,
                                                                 SocketHandle& handle,
                                                                 ByteSpan destination,
                                                                 NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitReceive(ctx, *m_impl, handle, destination, token);
    }

    NGIN::Async::Task<NGIN::UInt32> NetworkDriver::SubmitSendTo(NGIN::Async::TaskContext& ctx,
                                                                SocketHandle& handle,
                                                                Endpoint remoteEndpoint,
                                                                ConstByteSpan data,
                                                                NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitSendTo(ctx, *m_impl, handle, remoteEndpoint, data, token);
    }

    NGIN::Async::Task<DatagramReceiveResult> NetworkDriver::SubmitReceiveFrom(NGIN::Async::TaskContext& ctx,
                                                                              SocketHandle& handle,
                                                                              ByteSpan destination,
                                                                              NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitReceiveFrom(ctx, *m_impl, handle, destination, token);
    }

    NGIN::Async::Task<void> NetworkDriver::SubmitConnect(NGIN::Async::TaskContext& ctx,
                                                         SocketHandle& handle,
                                                         Endpoint remoteEndpoint,
                                                         NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitConnect(ctx, *m_impl, handle, remoteEndpoint, token);
    }

    NGIN::Async::Task<SocketHandle> NetworkDriver::SubmitAccept(NGIN::Async::TaskContext& ctx,
                                                                SocketHandle& handle,
                                                                NGIN::Async::CancellationToken token)
    {
        return Impl::SubmitAccept(ctx, *m_impl, handle, token);
    }
#endif
}

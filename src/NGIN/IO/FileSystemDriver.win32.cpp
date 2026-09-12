#include "NativeFileSystemBackend.hpp"
#include "RuntimeLoop.hpp"
#include <NGIN/Async/Cancellation.hpp>

#if defined(NGIN_PLATFORM_WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cassert>
#include <memory>
#include <mutex>

namespace NGIN::IO::detail
{
    // Each OVERLAPPED is a bounded runtime registration. No separate port,
    // completion thread, handle scan, or persistent idle-handle registration.
    class IocpNativeFileBackend final : public NativeFileBackend
    {
        struct Operation final : RuntimeLoop::Handler
        {
            explicit Operation(IocpNativeFileBackend& backend, NativeFileRequest submitted)
                : owner(backend), request(std::move(submitted)) {}

            void Ready(const RuntimePoller::Event& event) noexcept override
            {
                owner.Complete(*this, event);
            }
            void Stop() noexcept override
            {
                RequestCancellation();
            }
            void RequestCancellation() noexcept
            {
                std::lock_guard lock(owner.m_mutex);
                cancellationRequested = true;
                if (submitted && !done)
                {
                    // CancelIoEx requests cancellation. The OVERLAPPED, native
                    // handle, and buffers stay owned until its terminal packet.
                    (void) ::CancelIoEx(reinterpret_cast<HANDLE>(request.handleValue), &overlapped);
                }
            }
            IocpNativeFileBackend&                owner;
            NativeFileRequest                     request;
            OVERLAPPED                            overlapped {};
            std::uint64_t                         identifier {};
            bool                                  done {false};
            bool                                  submitted {false};
            bool                                  cancellationRequested {false};
            NGIN::Async::CancellationRegistration cancellation;
        };

    public:
        explicit IocpNativeFileBackend(FileSystemDriver& driver)
            : m_driver(driver), m_loop(RuntimeAccess::Loop(driver.GetRuntime())),
              m_capacity(driver.GetRuntime().GetOptions().files.queueDepthHint) {}

        ~IocpNativeFileBackend() override
        {
            assert(m_pending == 0);
        }

        [[nodiscard]] FileSystemDriver::ActiveBackend GetActiveBackend() const noexcept override
        {
            return FileSystemDriver::ActiveBackend::NativeIocp;
        }

        [[nodiscard]] bool Submit(NativeFileRequest request, const NGIN::Async::CancellationToken& token) noexcept override
        {
            if (request.kind == NativeFileOperationKind::Flush || request.kind == NativeFileOperationKind::Close)
                return SubmitControl(std::move(request));
            if (request.kind != NativeFileOperationKind::Read && request.kind != NativeFileOperationKind::Write)
                return false;
            std::shared_ptr<Operation> operation;
            std::unique_lock           lock(m_mutex);
            if (m_pending == m_capacity || m_driver.IsStopping())
                return false;
            try
            {
                operation = std::make_shared<Operation>(*this, std::move(request));
            } catch (const std::bad_alloc&)
            {
                return false;
            }
            // Registration may invoke immediately. Preparing cancellation only
            // marks the request; publication and OS submission share the mutex.
            lock.unlock();
            const auto cancellation = token.Register(operation->cancellation, {}, {}, +[](void* raw) noexcept {
                    static_cast<Operation*>(raw)->RequestCancellation();
                    return false; }, operation.get());
            lock.lock();
            if (!cancellation || operation->cancellationRequested || m_driver.IsStopping())
            {
                operation->done = true;
                NativeFileCompletion completion;
                if (!cancellation)
                    completion.fault = NGIN::Async::MakeAsyncFault(
                            NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed, static_cast<int>(cancellation.error()));
                else
                {
                    completion.status     = NativeFileCompletion::Status::Completed;
                    completion.systemCode = ERROR_OPERATION_ABORTED;
                }
                lock.unlock();
                operation->cancellation.Reset();
                operation->request.completion(operation->request.userData, std::move(completion));
                return true;
            }
            if (m_pending == m_capacity)
                return false;
            auto registered = m_loop.WatchCompletion(operation->request.handleValue, &operation->overlapped, operation);
            if (!registered)
            {
                NativeFileCompletion completion;
                completion.fault = NGIN::Async::MakeAsyncFault(
                        NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed,
                        registered.error() == std::errc::no_buffer_space || registered.error() == std::errc::not_enough_memory
                                ? static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted)
                                : registered.error().value());
                lock.unlock();
                operation->cancellation.Reset();
                operation->request.completion(operation->request.userData, std::move(completion));
                return true;
            }
            operation->identifier            = *registered;
            operation->overlapped.Offset     = static_cast<DWORD>(operation->request.offset & 0xffffffffULL);
            operation->overlapped.OffsetHigh = static_cast<DWORD>(operation->request.offset >> 32u);
            ++m_pending;
            const HANDLE handle  = reinterpret_cast<HANDLE>(operation->request.handleValue);
            operation->submitted = true;
            const BOOL  accepted = operation->request.kind == NativeFileOperationKind::Read
                                           ? ::ReadFile(handle, operation->request.buffer, operation->request.size, nullptr, &operation->overlapped)
                                           : ::WriteFile(handle, operation->request.buffer, operation->request.size, nullptr, &operation->overlapped);
            const DWORD error    = accepted ? ERROR_SUCCESS : ::GetLastError();
            if (error != ERROR_SUCCESS && error != ERROR_IO_PENDING)
            {
                // A synchronous failure posts no packet. Successful synchronous
                // completion still posts one and must follow the normal path.
                operation->done = true;
                --m_pending;
                m_loop.Unwatch(operation->identifier);
                NativeFileCompletion completion;
                completion.status     = NativeFileCompletion::Status::Completed;
                completion.value      = -1;
                completion.systemCode = static_cast<int>(error);
                lock.unlock();
                operation->cancellation.Reset();
                operation->request.completion(operation->request.userData, std::move(completion));
            }
            return true;
        }

    private:
        void Complete(Operation& operation, const RuntimePoller::Event& event) noexcept
        {
            NativeFileRequest request;
            {
                std::lock_guard lock(m_mutex);
                if (operation.done || event.operation != &operation.overlapped || event.identifier != operation.identifier)
                    return;
                operation.done = true;
                --m_pending;
                request = std::move(operation.request);
                m_loop.Unwatch(operation.identifier);
            }
            operation.cancellation.Reset();
            NativeFileCompletion completion;
            completion.status     = NativeFileCompletion::Status::Completed;
            completion.value      = event.error == 0 ? static_cast<Int64>(event.bytes) : -1;
            completion.systemCode = event.error;
            request.completion(request.userData, std::move(completion));
        }

        bool SubmitControl(NativeFileRequest request) noexcept
        {
            // FlushFileBuffers and CloseHandle are blocking APIs, not IOCP
            // operations. Use the driver's lazy, bounded workers and existing
            // common completion reservation already held by NativeFileAwaiter.
            try
            {
                auto accepted = m_driver.Execute(NGIN::Execution::WorkItem([request]() mutable noexcept {
                    const HANDLE         handle  = reinterpret_cast<HANDLE>(request.handleValue);
                    const BOOL           success = request.kind == NativeFileOperationKind::Flush
                                                           ? ::FlushFileBuffers(handle)
                                                           : ::CloseHandle(handle);
                    NativeFileCompletion completion;
                    completion.status     = NativeFileCompletion::Status::Completed;
                    completion.value      = success ? 0 : -1;
                    completion.systemCode = success ? 0 : static_cast<int>(::GetLastError());
                    request.completion(request.userData, std::move(completion));
                }));
                return accepted.has_value();
            } catch (const std::bad_alloc&)
            {
                return false;
            }
        }

        FileSystemDriver& m_driver;
        RuntimeLoop&      m_loop;
        const std::size_t m_capacity;
        std::mutex        m_mutex;
        std::size_t       m_pending {};
    };

    std::shared_ptr<NativeFileBackend> CreateNativeFileBackend(FileSystemDriver& driver)
    {
        return std::make_shared<IocpNativeFileBackend>(driver);
    }
}// namespace NGIN::IO::detail

#endif

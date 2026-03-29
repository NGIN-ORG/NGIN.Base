#include "NativeFileSystemBackend.hpp"

#if defined(NGIN_PLATFORM_WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <NGIN/Async/AsyncError.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace NGIN::IO::detail
{
    namespace
    {
        struct ControlRequest
        {
            NativeFileRequest request {};
        };

        struct OverlappedRequest
        {
            OVERLAPPED        overlapped {};
            NativeFileRequest request {};
        };
    }// namespace

    class IocpNativeFileBackend final : public NativeFileBackend
    {
    public:
        explicit IocpNativeFileBackend(const FileSystemDriver::Options&)
        {
            m_completionPort = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
            if (m_completionPort == nullptr)
            {
                return;
            }

            m_worker      = std::thread([this]() noexcept { Run(); });
            m_initialized = true;
        }

        ~IocpNativeFileBackend() override
        {
            m_stopping = true;
            if (m_completionPort != nullptr)
            {
                (void) ::PostQueuedCompletionStatus(m_completionPort, 0, kStopKey, nullptr);
            }
            if (m_worker.joinable())
            {
                m_worker.join();
            }
            if (m_completionPort != nullptr)
            {
                ::CloseHandle(m_completionPort);
            }
        }

        [[nodiscard]] FileSystemDriver::ActiveBackend GetActiveBackend() const noexcept override
        {
            return m_initialized ? FileSystemDriver::ActiveBackend::NativeIocp : FileSystemDriver::ActiveBackend::None;
        }

        [[nodiscard]] bool Submit(NativeFileRequest request) noexcept override
        {
            if (!m_initialized)
            {
                return false;
            }

            switch (request.kind)
            {
                case NativeFileOperationKind::Read:
                case NativeFileOperationKind::Write:
                    return SubmitOverlapped(std::move(request));
                case NativeFileOperationKind::Flush:
                case NativeFileOperationKind::Close:
                    return SubmitControl(std::move(request));
            }
            return false;
        }

    private:
        static constexpr ULONG_PTR kControlKey = 1;
        static constexpr ULONG_PTR kStopKey    = 2;

        bool AssociateHandle(HANDLE handle) noexcept
        {
            if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
            {
                return false;
            }

            {
                std::lock_guard<std::mutex> guard(m_associatedHandlesMutex);
                if (m_associatedHandles.contains(handle))
                {
                    return true;
                }
            }

            if (::CreateIoCompletionPort(handle, m_completionPort, 0, 0) != m_completionPort)
            {
                return false;
            }

            std::lock_guard<std::mutex> guard(m_associatedHandlesMutex);
            m_associatedHandles.insert(handle);
            return true;
        }

        void ForgetHandle(HANDLE handle) noexcept
        {
            if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
            {
                return;
            }

            std::lock_guard<std::mutex> guard(m_associatedHandlesMutex);
            m_associatedHandles.erase(handle);
        }

        bool SubmitOverlapped(NativeFileRequest request) noexcept
        {
            auto* op = new OverlappedRequest {};
            op->request = std::move(request);

            const auto handle = reinterpret_cast<HANDLE>(op->request.handleValue);
            if (!AssociateHandle(handle))
            {
                CompleteFault(op->request, NGIN::Async::AsyncFaultCode::SchedulerFailure, static_cast<int>(::GetLastError()));
                delete op;
                return false;
            }

            if (!op->request.useCurrentOffset)
            {
                op->overlapped.Offset     = static_cast<DWORD>(op->request.offset & 0xffffffffULL);
                op->overlapped.OffsetHigh = static_cast<DWORD>((op->request.offset >> 32u) & 0xffffffffULL);
            }

            DWORD ignored = 0;
            BOOL ok = FALSE;
            if (op->request.kind == NativeFileOperationKind::Read)
            {
                ok = ::ReadFile(handle, op->request.buffer, op->request.size, &ignored, &op->overlapped);
            }
            else
            {
                ok = ::WriteFile(handle, op->request.buffer, op->request.size, &ignored, &op->overlapped);
            }

            if (!ok)
            {
                const auto error = ::GetLastError();
                if (error != ERROR_IO_PENDING)
                {
                    NativeFileCompletion completion;
                    completion.status     = NativeFileCompletion::Status::Completed;
                    completion.value      = -1;
                    completion.systemCode = static_cast<int>(error);
                    op->request.completion(op->request.userData, completion);
                    delete op;
                    return true;
                }
            }
            return true;
        }

        bool SubmitControl(NativeFileRequest request) noexcept
        {
            auto* control = new ControlRequest {};
            control->request = std::move(request);
            if (!::PostQueuedCompletionStatus(m_completionPort, 0, kControlKey, reinterpret_cast<LPOVERLAPPED>(control)))
            {
                CompleteFault(control->request, NGIN::Async::AsyncFaultCode::SchedulerFailure, static_cast<int>(::GetLastError()));
                delete control;
                return false;
            }
            return true;
        }

        static void CompleteFault(NativeFileRequest& request, NGIN::Async::AsyncFaultCode code, const int native = 0) noexcept
        {
            if (request.completion == nullptr)
            {
                return;
            }
            request.completion(
                    request.userData,
                    NativeFileCompletion {
                            .status = NativeFileCompletion::Status::Fault,
                            .fault = NGIN::Async::MakeAsyncFault(code, native),
                    });
        }

        void Run() noexcept
        {
            while (!m_stopping)
            {
                DWORD       bytes      = 0;
                ULONG_PTR   key        = 0;
                LPOVERLAPPED overlapped = nullptr;
                const BOOL  ok         = ::GetQueuedCompletionStatus(m_completionPort, &bytes, &key, &overlapped, INFINITE);

                if (key == kStopKey)
                {
                    break;
                }

                if (key == kControlKey && overlapped != nullptr)
                {
                    auto* control = reinterpret_cast<ControlRequest*>(overlapped);
                    NativeFileCompletion completion;
                    completion.status = NativeFileCompletion::Status::Completed;

                    switch (control->request.kind)
                    {
                        case NativeFileOperationKind::Flush:
                            if (!::FlushFileBuffers(reinterpret_cast<HANDLE>(control->request.handleValue)))
                            {
                                completion.value      = -1;
                                completion.systemCode = static_cast<int>(::GetLastError());
                            }
                            break;
                        case NativeFileOperationKind::Close:
                        {
                            const auto handle = reinterpret_cast<HANDLE>(control->request.handleValue);
                            ForgetHandle(handle);
                            if (!::CloseHandle(handle))
                            {
                                completion.value      = -1;
                                completion.systemCode = static_cast<int>(::GetLastError());
                            }
                            break;
                        }
                        default:
                            break;
                    }

                    if (control->request.completion != nullptr)
                    {
                        control->request.completion(control->request.userData, completion);
                    }
                    delete control;
                    continue;
                }

                if (overlapped != nullptr)
                {
                    auto* op = reinterpret_cast<OverlappedRequest*>(overlapped);
                    NativeFileCompletion completion;
                    completion.status = NativeFileCompletion::Status::Completed;
                    if (!ok)
                    {
                        completion.value      = -1;
                        completion.systemCode = static_cast<int>(::GetLastError());
                    }
                    else
                    {
                        completion.value = static_cast<Int64>(bytes);
                    }
                    if (op->request.completion != nullptr)
                    {
                        op->request.completion(op->request.userData, completion);
                    }
                    delete op;
                }
            }
        }

        HANDLE      m_completionPort {nullptr};
        bool        m_initialized {false};
        bool        m_stopping {false};
        std::thread m_worker {};
        std::mutex  m_associatedHandlesMutex {};
        std::unordered_set<HANDLE> m_associatedHandles {};
    };

    std::unique_ptr<NativeFileBackend> CreateNativeFileBackend(const FileSystemDriver::Options& options)
    {
        auto backend = std::make_unique<IocpNativeFileBackend>(options);
        if (backend->GetActiveBackend() != FileSystemDriver::ActiveBackend::None)
        {
            return backend;
        }
        return {};
    }
}// namespace NGIN::IO::detail

#endif

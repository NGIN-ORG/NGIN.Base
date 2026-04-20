#include <NGIN/IO/LocalFileSystem.hpp>

#include "AsyncDispatch.hpp"
#include "LocalFileSystem.posix.internal.hpp"
#include "NativeFileSystemBackend.hpp"

#if defined(__linux__)

namespace NGIN::IO
{
    namespace
    {
        using detail::LocalAsyncFileState;

        struct NativePosixFileCompletion
        {
            enum class Status : UInt8
            {
                Completed,
                Canceled,
                Fault,
            };

            Status                  status {Status::Fault};
            Int64                   value {0};
            int                     systemCode {0};
            NGIN::Async::AsyncFault fault {};
        };

        class NativePosixFileAwaiter
        {
        public:
            NativePosixFileAwaiter(detail::NativeFileBackend& backend,
                                   NGIN::Async::TaskContext& ctx,
                                   detail::NativeFileRequest request) noexcept
                : m_backend(backend)
                , m_resumeExecutor(ctx.GetExecutor())
                , m_cancellation(ctx.GetCancellationToken())
                , m_request(std::move(request))
                , m_state(std::make_shared<State>())
            {
            }

            [[nodiscard]] bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> awaiting) noexcept
            {
                m_state->m_resumeExecutor = m_resumeExecutor;
                m_state->m_awaiting       = awaiting;

                if (m_cancellation.IsCancellationRequested())
                {
                    m_state->completion.status = NativePosixFileCompletion::Status::Canceled;
                    m_state->Resume();
                    return;
                }

                m_request.userData   = m_state.get();
                m_request.completion = +[](void* rawState, detail::NativeFileCompletion completion) noexcept {
                    auto* state = static_cast<State*>(rawState);
                    if (state == nullptr)
                    {
                        return;
                    }
                    if (completion.status == detail::NativeFileCompletion::Status::Fault)
                    {
                        state->completion.status = NativePosixFileCompletion::Status::Fault;
                        state->completion.fault  = completion.fault;
                    }
                    else
                    {
                        state->completion.status     = NativePosixFileCompletion::Status::Completed;
                        state->completion.value      = completion.value;
                        state->completion.systemCode = completion.systemCode;
                    }
                    state->Resume();
                };

                if (!m_backend.Submit(m_request))
                {
                    m_state->completion.status = NativePosixFileCompletion::Status::Fault;
                    m_state->completion.fault  = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::SchedulerFailure);
                    m_state->Resume();
                }
            }

            [[nodiscard]] NativePosixFileCompletion await_resume() noexcept
            {
                return std::move(m_state->completion);
            }

        private:
            struct State
            {
                NGIN::Execution::ExecutorRef m_resumeExecutor {};
                std::coroutine_handle<>      m_awaiting {};
                NativePosixFileCompletion    completion {};

                void Resume() noexcept
                {
                    if (m_awaiting)
                    {
                        if (m_resumeExecutor.IsValid())
                        {
                            m_resumeExecutor.Execute(m_awaiting);
                        }
                        else
                        {
                            m_awaiting.resume();
                        }
                    }
                }
            };

            detail::NativeFileBackend&     m_backend;
            NGIN::Execution::ExecutorRef   m_resumeExecutor {};
            NGIN::Async::CancellationToken m_cancellation {};
            detail::NativeFileRequest      m_request {};
            std::shared_ptr<State>         m_state {};
        };

        [[nodiscard]] auto SubmitNativePosixFile(detail::NativeFileBackend& backend,
                                                 NGIN::Async::TaskContext& ctx,
                                                 detail::NativeFileRequest request) noexcept
        {
            return NativePosixFileAwaiter(backend, ctx, std::move(request));
        }

        AsyncTask<UIntSize> LocalAsyncFileRead(
                const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Read,
                                .handleValue = static_cast<std::uintptr_t>(state->fd),
                                .useCurrentOffset = true,
                                .buffer = destination.data(),
                                .size = static_cast<UInt32>(destination.size()),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await AsyncTask<UIntSize>::ReturnCanceled();
                    co_return 0;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await AsyncTask<UIntSize>::ReturnFault(completion.fault);
                    co_return 0;
                }
                if (completion.systemCode != 0)
                {
                    co_return NGIN::Utilities::Unexpected<IOError>(
                            detail::MakeErrnoError(completion.systemCode, "io_uring read failed", state->path));
                }
                co_return static_cast<UIntSize>(completion.value);
            }

            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, destination]() mutable noexcept {
                return detail::LocalAsyncFileReadSync(*state, destination);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<UIntSize>::ReturnCanceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<UIntSize>::ReturnFault(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTask<UIntSize> LocalAsyncFileWrite(
                const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Write,
                                .handleValue = static_cast<std::uintptr_t>(state->fd),
                                .useCurrentOffset = true,
                                .buffer = const_cast<NGIN::Byte*>(source.data()),
                                .size = static_cast<UInt32>(source.size()),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await AsyncTask<UIntSize>::ReturnCanceled();
                    co_return 0;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await AsyncTask<UIntSize>::ReturnFault(completion.fault);
                    co_return 0;
                }
                if (completion.systemCode != 0)
                {
                    co_return NGIN::Utilities::Unexpected<IOError>(
                            detail::MakeErrnoError(completion.systemCode, "io_uring write failed", state->path));
                }
                co_return static_cast<UIntSize>(completion.value);
            }

            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, source]() mutable noexcept {
                return detail::LocalAsyncFileWriteSync(*state, source);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<UIntSize>::ReturnCanceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<UIntSize>::ReturnFault(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTask<UIntSize> LocalAsyncFileReadAt(const std::shared_ptr<void>& rawState,
                                                 NGIN::Async::TaskContext&  ctx,
                                                 UInt64                     offset,
                                                 std::span<NGIN::Byte>      destination)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Read,
                                .handleValue = static_cast<std::uintptr_t>(state->fd),
                                .offset = offset,
                                .buffer = destination.data(),
                                .size = static_cast<UInt32>(destination.size()),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await AsyncTask<UIntSize>::ReturnCanceled();
                    co_return 0;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await AsyncTask<UIntSize>::ReturnFault(completion.fault);
                    co_return 0;
                }
                if (completion.systemCode != 0)
                {
                    co_return NGIN::Utilities::Unexpected<IOError>(
                            detail::MakeErrnoError(completion.systemCode, "io_uring read failed", state->path));
                }
                co_return static_cast<UIntSize>(completion.value);
            }

            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, offset, destination]() mutable noexcept {
                return detail::LocalAsyncFileReadAtSync(*state, offset, destination);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<UIntSize>::ReturnCanceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<UIntSize>::ReturnFault(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTask<UIntSize> LocalAsyncFileWriteAt(const std::shared_ptr<void>& rawState,
                                                  NGIN::Async::TaskContext&  ctx,
                                                  UInt64                     offset,
                                                  std::span<const NGIN::Byte> source)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Write,
                                .handleValue = static_cast<std::uintptr_t>(state->fd),
                                .offset = offset,
                                .buffer = const_cast<NGIN::Byte*>(source.data()),
                                .size = static_cast<UInt32>(source.size()),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await AsyncTask<UIntSize>::ReturnCanceled();
                    co_return 0;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await AsyncTask<UIntSize>::ReturnFault(completion.fault);
                    co_return 0;
                }
                if (completion.systemCode != 0)
                {
                    co_return NGIN::Utilities::Unexpected<IOError>(
                            detail::MakeErrnoError(completion.systemCode, "io_uring write failed", state->path));
                }
                co_return static_cast<UIntSize>(completion.value);
            }

            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state, offset, source]() mutable noexcept {
                return detail::LocalAsyncFileWriteAtSync(*state, offset, source);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTask<UIntSize>::ReturnCanceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await AsyncTask<UIntSize>::ReturnFault(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
            co_return std::move(result).TakeValue();
        }

        AsyncTaskVoid LocalAsyncFileFlush(const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Flush,
                                .handleValue = static_cast<std::uintptr_t>(state->fd),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await AsyncTaskVoid::ReturnCanceled();
                    co_return;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await AsyncTaskVoid::ReturnFault(completion.fault);
                    co_return;
                }
                if (completion.systemCode != 0)
                {
                    co_await AsyncTaskVoid::ReturnError(detail::MakeErrnoError(completion.systemCode, "io_uring fsync failed", state->path));
                    co_return;
                }
                co_return;
            }

            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state]() mutable noexcept {
                return detail::LocalAsyncFileFlushSync(*state);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTaskVoid::ReturnCanceled();
                co_return;
            }
            if (completion.IsFault())
            {
                co_await AsyncTaskVoid::ReturnFault(std::move(*completion.fault));
                co_return;
            }
            auto result = std::move(*completion.result);
            if (!result)
            {
                co_await AsyncTaskVoid::ReturnError(std::move(result).TakeError());
                co_return;
            }
            co_return;
        }

        AsyncTaskVoid LocalAsyncFileClose(const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                int fdToClose = -1;
                {
                    std::lock_guard<std::mutex> guard(state->mutex);
                    fdToClose = state->fd;
                    state->fd = -1;
                }
                if (fdToClose < 0)
                {
                    co_return;
                }

                auto completion = co_await SubmitNativePosixFile(
                        *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind = detail::NativeFileOperationKind::Close,
                                .handleValue = static_cast<std::uintptr_t>(fdToClose),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await AsyncTaskVoid::ReturnCanceled();
                    co_return;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await AsyncTaskVoid::ReturnFault(completion.fault);
                    co_return;
                }
                if (completion.systemCode != 0)
                {
                    co_await AsyncTaskVoid::ReturnError(detail::MakeErrnoError(completion.systemCode, "io_uring close failed", state->path));
                    co_return;
                }
                co_return;
            }

            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state]() mutable noexcept {
                return detail::LocalAsyncFileCloseSync(*state);
            });
            if (completion.IsCanceled())
            {
                co_await AsyncTaskVoid::ReturnCanceled();
                co_return;
            }
            if (completion.IsFault())
            {
                co_await AsyncTaskVoid::ReturnFault(std::move(*completion.fault));
                co_return;
            }
            auto result = std::move(*completion.result);
            if (!result)
            {
                co_await AsyncTaskVoid::ReturnError(std::move(result).TakeError());
                co_return;
            }
            co_return;
        }

        [[nodiscard]] bool LocalAsyncFileIsOpen(const std::shared_ptr<void>& rawState) noexcept
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (!state)
            {
                return false;
            }
            std::lock_guard<std::mutex> guard(state->mutex);
            return state->fd >= 0;
        }

        const AsyncFileHandle::Operations LocalAsyncFileOperations {
                .read = &LocalAsyncFileRead,
                .write = &LocalAsyncFileWrite,
                .readAt = &LocalAsyncFileReadAt,
                .writeAt = &LocalAsyncFileWriteAt,
                .flush = &LocalAsyncFileFlush,
                .close = &LocalAsyncFileClose,
                .isOpen = &LocalAsyncFileIsOpen,
        };
    }

    [[nodiscard]] AsyncFileHandle detail::MakeAsyncPosixFileHandle(
            std::shared_ptr<FileSystemDriver> driver, OpenedAsyncPosixFile opened)
    {
        auto state = std::make_shared<LocalAsyncFileState>();
        state->driver   = std::move(driver);
        state->path     = std::move(opened.path);
        state->canRead  = opened.canRead;
        state->canWrite = opened.canWrite;
        state->fd       = opened.fd;
        return AsyncFileHandle(std::move(state), &LocalAsyncFileOperations);
    }

    AsyncTask<AsyncFileHandle> LocalFileSystem::OpenFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options)
    {
        auto completion = co_await detail::DispatchToDriver(*m_asyncDriver, ctx, [path, options]() mutable noexcept {
            return detail::OpenAsyncPosixFile(path, options);
        });

        if (completion.IsCanceled())
        {
            co_return NGIN::Async::Sentinels::Canceled;
        }
        if (completion.IsFault())
        {
            co_return NGIN::Async::Fault(std::move(*completion.fault));
        }

        auto opened = std::move(*completion.result);
        if (!opened)
        {
            co_return std::move(opened).TakeError();
        }

        co_return detail::MakeAsyncPosixFileHandle(m_asyncDriver, std::move(opened).TakeValue());
    }
}

#endif

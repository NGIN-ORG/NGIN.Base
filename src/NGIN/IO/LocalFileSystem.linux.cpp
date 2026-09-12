#include <NGIN/IO/LocalFileSystem.hpp>

#include "AsyncDispatch.hpp"
#include "LocalFileSystem.posix.internal.hpp"
#include "NativeFileDispatch.hpp"
#include "NativeFileSystemBackend.hpp"

#if defined(__linux__)

namespace NGIN::IO
{
    namespace
    {
        using detail::LocalAsyncFileState;

        using NativePosixFileCompletion = detail::NativeOperationCompletion;

        [[nodiscard]] auto SubmitNativePosixFile(detail::FileSystemDriver&  driver,
                                                 detail::NativeFileBackend& backend, NGIN::Async::TaskContext& ctx,
                                                 detail::NativeFileRequest request)
        {
            return detail::NativeFileAwaiter(driver, backend, ctx, std::move(request));
        }

        AsyncTask<UIntSize> LocalAsyncFileRead(
                const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto valid = detail::ValidateFileTransfer(state->path, state->canRead, false, state->appendMode, destination.size()); !valid)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(valid).error());
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *state->driver, *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind             = detail::NativeFileOperationKind::Read,
                                .handleValue      = static_cast<std::uintptr_t>(state->fd),
                                .useCurrentOffset = true,
                                .buffer           = destination.data(),
                                .size             = static_cast<UInt32>(destination.size()),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await NGIN::Async::Canceled();
                    co_return 0;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await NGIN::Async::Faulted(completion.fault);
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
                co_await NGIN::Async::Canceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await NGIN::Async::Faulted(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).error());
            co_return std::move(result).value();
        }

        AsyncTask<UIntSize> LocalAsyncFileWrite(
                const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto valid = detail::ValidateFileTransfer(state->path, state->canWrite, true, state->appendMode, source.size()); !valid)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(valid).error());
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *state->driver, *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind             = detail::NativeFileOperationKind::Write,
                                .handleValue      = static_cast<std::uintptr_t>(state->fd),
                                .useCurrentOffset = true,
                                .buffer           = const_cast<NGIN::Byte*>(source.data()),
                                .size             = static_cast<UInt32>(source.size()),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await NGIN::Async::Canceled();
                    co_return 0;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await NGIN::Async::Faulted(completion.fault);
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
                co_await NGIN::Async::Canceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await NGIN::Async::Faulted(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).error());
            co_return std::move(result).value();
        }

        AsyncTask<UIntSize> LocalAsyncFileReadAt(const std::shared_ptr<void>& rawState,
                                                 NGIN::Async::TaskContext&    ctx,
                                                 UInt64                       offset,
                                                 std::span<NGIN::Byte>        destination)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto valid = detail::ValidateFileTransfer(state->path, state->canRead, false, state->appendMode, destination.size(), offset); !valid)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(valid).error());
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *state->driver, *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind        = detail::NativeFileOperationKind::Read,
                                .handleValue = static_cast<std::uintptr_t>(state->fd),
                                .offset      = offset,
                                .buffer      = destination.data(),
                                .size        = static_cast<UInt32>(destination.size()),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await NGIN::Async::Canceled();
                    co_return 0;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await NGIN::Async::Faulted(completion.fault);
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
                co_await NGIN::Async::Canceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await NGIN::Async::Faulted(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).error());
            co_return std::move(result).value();
        }

        AsyncTask<UIntSize> LocalAsyncFileWriteAt(const std::shared_ptr<void>& rawState,
                                                  NGIN::Async::TaskContext&    ctx,
                                                  UInt64                       offset,
                                                  std::span<const NGIN::Byte>  source)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto valid = detail::ValidateFileTransfer(state->path, state->canWrite, true, state->appendMode, source.size(), offset); !valid)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(valid).error());
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *state->driver, *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind        = detail::NativeFileOperationKind::Write,
                                .handleValue = static_cast<std::uintptr_t>(state->fd),
                                .offset      = offset,
                                .buffer      = const_cast<NGIN::Byte*>(source.data()),
                                .size        = static_cast<UInt32>(source.size()),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await NGIN::Async::Canceled();
                    co_return 0;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await NGIN::Async::Faulted(completion.fault);
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
                co_await NGIN::Async::Canceled();
                co_return 0;
            }
            if (completion.IsFault())
            {
                co_await NGIN::Async::Faulted(std::move(*completion.fault));
                co_return 0;
            }
            auto result = std::move(*completion.result);
            if (!result)
                co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).error());
            co_return std::move(result).value();
        }

        AsyncTaskVoid LocalAsyncFileFlush(const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx)
        {
            auto state = std::static_pointer_cast<LocalAsyncFileState>(rawState);
            if (auto* backend = detail::GetNativeFileBackend(*state->driver); backend != nullptr)
            {
                auto completion = co_await SubmitNativePosixFile(
                        *state->driver, *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind        = detail::NativeFileOperationKind::Flush,
                                .handleValue = static_cast<std::uintptr_t>(state->fd),
                        });
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await NGIN::Async::Faulted(completion.fault);
                    co_return;
                }
                if (completion.systemCode != 0)
                {
                    co_await NGIN::Async::DomainFailure(detail::MakeErrnoError(completion.systemCode, "io_uring fsync failed", state->path));
                    co_return;
                }
                co_return;
            }

            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state]() mutable noexcept {
                return detail::LocalAsyncFileFlushSync(*state);
            });
            if (completion.IsCanceled())
            {
                co_await NGIN::Async::Canceled();
                co_return;
            }
            if (completion.IsFault())
            {
                co_await NGIN::Async::Faulted(std::move(*completion.fault));
                co_return;
            }
            auto result = std::move(*completion.result);
            if (!result)
            {
                co_await NGIN::Async::DomainFailure(std::move(result).error());
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

                struct RestoreUnsubmittedClose
                {
                    LocalAsyncFileState& state;
                    decltype(fdToClose)  original;
                    bool                 submitted {false};
                    ~RestoreUnsubmittedClose()
                    {
                        if (!submitted)
                        {
                            std::lock_guard lock(state.mutex);
                            state.fd = original;
                        }
                    }
                } restore {*state, fdToClose};
                auto completion = co_await SubmitNativePosixFile(
                        *state->driver, *backend,
                        ctx,
                        detail::NativeFileRequest {
                                .kind        = detail::NativeFileOperationKind::Close,
                                .handleValue = static_cast<std::uintptr_t>(fdToClose),
                        });
                restore.submitted = completion.submitted;
                if (completion.status == NativePosixFileCompletion::Status::Canceled)
                {
                    co_await NGIN::Async::Canceled();
                    co_return;
                }
                if (completion.status == NativePosixFileCompletion::Status::Fault)
                {
                    co_await NGIN::Async::Faulted(completion.fault);
                    co_return;
                }
                if (completion.systemCode != 0)
                {
                    co_await NGIN::Async::DomainFailure(detail::MakeErrnoError(completion.systemCode, "io_uring close failed", state->path));
                    co_return;
                }
                co_return;
            }

            auto completion = co_await detail::DispatchToDriver(*state->driver, ctx, [state]() mutable noexcept {
                return detail::LocalAsyncFileCloseSync(*state);
            });
            if (completion.IsCanceled())
            {
                co_await NGIN::Async::Canceled();
                co_return;
            }
            if (completion.IsFault())
            {
                co_await NGIN::Async::Faulted(std::move(*completion.fault));
                co_return;
            }
            auto result = std::move(*completion.result);
            if (!result)
            {
                co_await NGIN::Async::DomainFailure(std::move(result).error());
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
            return !state->operations.IsClosing() && state->NativeIsOpen();
        }

        const AsyncFileHandle::Operations LocalAsyncFileOperations {
                .read    = &detail::WithFileOperation<UIntSize, LocalAsyncFileState, detail::FileOperationGate::Kind::Position,
                                                      &LocalAsyncFileRead, std::span<NGIN::Byte>>,
                .write   = &detail::WithFileOperation<UIntSize, LocalAsyncFileState, detail::FileOperationGate::Kind::Position,
                                                      &LocalAsyncFileWrite, std::span<const NGIN::Byte>>,
                .readAt  = &detail::WithFileOperation<UIntSize, LocalAsyncFileState, detail::FileOperationGate::Kind::Independent,
                                                      &LocalAsyncFileReadAt, UInt64, std::span<NGIN::Byte>>,
                .writeAt = &detail::WithFileOperation<UIntSize, LocalAsyncFileState, detail::FileOperationGate::Kind::Independent,
                                                      &LocalAsyncFileWriteAt, UInt64, std::span<const NGIN::Byte>>,
                .flush   = &detail::WithFileOperation<void, LocalAsyncFileState, detail::FileOperationGate::Kind::Flush,
                                                      &LocalAsyncFileFlush>,
                .close   = &detail::WithFileOperation<void, LocalAsyncFileState, detail::FileOperationGate::Kind::Close,
                                                      &LocalAsyncFileClose>,
                .isOpen  = &LocalAsyncFileIsOpen,
        };
    }// namespace

    [[nodiscard]] AsyncFileHandle detail::MakeAsyncPosixFileHandle(
            std::shared_ptr<NGIN::IO::detail::FileSystemDriver> driver, OpenedAsyncPosixFile opened)
    {
        auto state        = std::make_shared<LocalAsyncFileState>(std::move(driver));
        state->path       = std::move(opened.path);
        state->canRead    = opened.canRead;
        state->canWrite   = opened.canWrite;
        state->appendMode = opened.appendMode;
        state->fd         = std::exchange(opened.fd, -1);
        return AsyncFileHandle(std::move(state), &LocalAsyncFileOperations);
    }

    AsyncTask<AsyncFileHandle> LocalFileSystem::OpenFileAsync(
            NGIN::Async::TaskContext& ctx, Path path, FileOpenOptions options)
    {
        const std::shared_ptr<NGIN::IO::detail::FileSystemDriver> driver = AcquireDriver();
        if (!driver)
        {
            const auto fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidTaskUsage, 0,
                                                           "Async filesystem operations require a bound, running IO::Runtime");
            co_return NGIN::Async::Completion<AsyncFileHandle, IOError>::Faulted(fault);
        }

        auto completion = co_await detail::DispatchToDriver(
                *driver, ctx, [path = std::move(path), options]() mutable noexcept {
                    return detail::OpenAsyncPosixFile(path, options);
                });

        if (completion.IsCanceled())
        {
            co_return NGIN::Async::Completion<AsyncFileHandle, IOError>::Canceled();
        }
        if (completion.IsFault())
        {
            co_return NGIN::Async::Completion<AsyncFileHandle, IOError>::Faulted(std::move(*completion.fault));
        }

        auto opened = std::move(*completion.result);
        if (!opened)
        {
            co_return std::move(opened).error();
        }

        co_return detail::MakeAsyncPosixFileHandle(driver, std::move(opened).value());
    }
}// namespace NGIN::IO

#endif

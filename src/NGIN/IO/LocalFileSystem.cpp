#include <NGIN/IO/LocalFileSystem.hpp>

#include "AsyncDispatch.hpp"
#include "FileSystemDriver.hpp"

namespace NGIN::IO
{
    LocalFileSystem::LocalFileSystem() noexcept = default;
    LocalFileSystem::LocalFileSystem(Runtime& runtime) noexcept : m_runtime(&runtime) {}
    std::shared_ptr<NGIN::IO::detail::FileSystemDriver> LocalFileSystem::AcquireDriver() const
    {
        return m_runtime ? detail::AcquireFileSystemDriver(*m_runtime) : nullptr;
    }

    ResultVoid LocalFileSystem::Move(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        auto renamed = options.overwriteExisting ? Rename(from, to) : RenameNoReplace(from, to);
        if (renamed.has_value())
            return renamed;
        if (renamed.error().code != IOErrorCode::CrossDevice)
            return renamed;

        auto copied = CopyFile(from, to, options);
        if (!copied.has_value())
            return copied;

        RemoveOptions removeOptions;
        removeOptions.recursive     = options.recursive;
        removeOptions.ignoreMissing = false;

        MetadataOptions metadataOptions;
        metadataOptions.symlinkMode = SymlinkMode::DoNotFollow;
        auto infoResult             = GetInfo(from, metadataOptions);
        if (!infoResult.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(infoResult.error())));

        if (infoResult.value().type == EntryType::Directory)
            return RemoveDirectory(from, removeOptions);
        return RemoveFile(from, removeOptions);
    }

    Result<FileView> LocalFileSystem::OpenFileView(const Path& path) noexcept
    {
        FileView view;
        auto     result = view.Open(path);
        if (!result.has_value())
            return Result<FileView>(NGIN::Utilities::Unexpected<IOError>(std::move(result.error())));
        return Result<FileView>(std::move(view));
    }

    AsyncTask<FileInfo> LocalFileSystem::GetInfoAsync(
            NGIN::Async::TaskContext& ctx, Path path, MetadataOptions options)
    {
        const std::shared_ptr<NGIN::IO::detail::FileSystemDriver> driver = AcquireDriver();
        if (!driver)
        {
            const auto fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidTaskUsage, 0,
                                                           "Async filesystem operations require a bound, running IO::Runtime");
            co_return NGIN::Async::Completion<FileInfo, IOError>::Faulted(fault);
        }

        auto completion = co_await detail::DispatchToDriver(
                *driver, ctx, [this, path = std::move(path), options]() mutable noexcept {
                    return GetInfo(path, options);
                });

        if (completion.IsCanceled())
        {
            co_return NGIN::Async::Completion<FileInfo, IOError>::Canceled();
        }

        if (completion.IsFault())
        {
            co_return NGIN::Async::Completion<FileInfo, IOError>::Faulted(std::move(*completion.fault));
        }

        auto result = std::move(*completion.result);
        if (!result)
        {
            co_return std::move(result).error();
        }
        co_return std::move(result).value();
    }

    AsyncTaskVoid LocalFileSystem::CopyFileAsync(
            NGIN::Async::TaskContext& ctx, Path from, Path to, CopyOptions options)
    {
        const std::shared_ptr<NGIN::IO::detail::FileSystemDriver> driver = AcquireDriver();
        if (!driver)
        {
            const auto fault = NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidTaskUsage, 0,
                                                           "Async filesystem operations require a bound, running IO::Runtime");
            co_await NGIN::Async::Faulted(fault);
            co_return;
        }

        auto completion = co_await detail::DispatchToDriver(
                *driver,
                ctx,
                [this, from = std::move(from), to = std::move(to), options]() mutable noexcept {
                    return CopyFile(from, to, options);
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

        auto copied = std::move(*completion.result);
        if (!copied)
        {
            co_await NGIN::Async::DomainFailure(std::move(copied).error());
            co_return;
        }

        co_return;
    }
}// namespace NGIN::IO

#include <NGIN/IO/LocalFileSystem.hpp>

#include "AsyncDispatch.hpp"

namespace NGIN::IO
{
    LocalFileSystem::LocalFileSystem()
        : m_asyncDriver(std::make_shared<FileSystemDriver>())
    {
    }

    LocalFileSystem::LocalFileSystem(std::shared_ptr<FileSystemDriver> asyncDriver)
        : m_asyncDriver(std::move(asyncDriver))
    {
        if (!m_asyncDriver)
        {
            m_asyncDriver = std::make_shared<FileSystemDriver>();
        }
    }

    void LocalFileSystem::BindAsyncDriver(std::shared_ptr<FileSystemDriver> asyncDriver) noexcept
    {
        m_asyncDriver = std::move(asyncDriver);
        if (!m_asyncDriver)
        {
            m_asyncDriver = std::make_shared<FileSystemDriver>();
        }
    }

    ResultVoid LocalFileSystem::Move(const Path& from, const Path& to, const CopyOptions& options) noexcept
    {
        auto renamed = Rename(from, to);
        if (renamed.HasValue())
            return renamed;
        if (renamed.Error().code != IOErrorCode::CrossDevice)
            return renamed;

        auto copied = CopyFile(from, to, options);
        if (!copied.HasValue())
            return copied;

        RemoveOptions removeOptions;
        removeOptions.recursive     = options.recursive;
        removeOptions.ignoreMissing = false;

        MetadataOptions metadataOptions;
        metadataOptions.symlinkMode = SymlinkMode::DoNotFollow;
        auto infoResult             = GetInfo(from, metadataOptions);
        if (!infoResult.HasValue())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(infoResult.Error())));

        if (infoResult.Value().type == EntryType::Directory)
            return RemoveDirectory(from, removeOptions);
        return RemoveFile(from, removeOptions);
    }

    Result<FileView> LocalFileSystem::OpenFileView(const Path& path) noexcept
    {
        FileView view;
        auto     result = view.Open(path);
        if (!result.HasValue())
            return Result<FileView>(NGIN::Utilities::Unexpected<IOError>(std::move(result.Error())));
        return Result<FileView>(std::move(view));
    }

    AsyncTask<FileInfo> LocalFileSystem::GetInfoAsync(
            NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options)
    {
        auto completion = co_await detail::DispatchToDriver(*m_asyncDriver, ctx, [this, path, options]() mutable noexcept {
            return GetInfo(path, options);
        });

        if (completion.IsCanceled())
        {
            co_await AsyncTask<FileInfo>::ReturnCanceled();
            co_return FileInfo {};
        }

        if (completion.IsFault())
        {
            co_await AsyncTask<FileInfo>::ReturnFault(std::move(*completion.fault));
            co_return FileInfo {};
        }

        auto result = std::move(*completion.result);
        if (!result)
        {
            co_return NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError());
        }
        co_return std::move(result).TakeValue();
    }

    AsyncTaskVoid LocalFileSystem::CopyFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options)
    {
        auto completion = co_await detail::DispatchToDriver(*m_asyncDriver, ctx, [this, from, to, options]() mutable noexcept {
            return CopyFile(from, to, options);
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

        auto copied = std::move(*completion.result);
        if (!copied)
        {
            co_await AsyncTaskVoid::ReturnError(std::move(copied).TakeError());
            co_return;
        }

        co_return;
    }
}// namespace NGIN::IO

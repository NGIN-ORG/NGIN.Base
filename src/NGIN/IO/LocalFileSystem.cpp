#include <NGIN/IO/LocalFileSystem.hpp>

namespace NGIN::IO
{
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
        co_await ctx.YieldNow();
        co_return ToAsyncResult(GetInfo(path, options));
    }

    AsyncTaskVoid LocalFileSystem::CopyFileAsync(
            NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options)
    {
        co_await ctx.YieldNow();

        auto copied = CopyFile(from, to, options);
        if (!copied)
        {
            co_await AsyncTaskVoid::ReturnError(std::move(copied).TakeError());
            co_return;
        }

        co_return;
    }
}// namespace NGIN::IO

#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/IO/AsyncDirectoryHandle.hpp>
#include <NGIN/IO/AsyncFileHandle.hpp>
#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/IOResult.hpp>

namespace NGIN::IO
{
    class NGIN_BASE_API IAsyncFileSystem
    {
    public:
        virtual ~IAsyncFileSystem() = default;

        virtual AsyncTask<AsyncFileHandle> OpenFileAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options) = 0;
        virtual AsyncTask<AsyncDirectoryHandle> OpenDirectoryAsync(
                NGIN::Async::TaskContext& ctx, const Path& path) = 0;
        virtual AsyncTask<FileInfo>  GetInfoAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options = {}) = 0;
        virtual AsyncTaskVoid        CopyFileAsync(NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options = {}) = 0;
    };
}// namespace NGIN::IO

#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/IAsyncFileHandle.hpp>
#include <NGIN/IO/IOResult.hpp>

#include <memory>

namespace NGIN::IO
{
    class NGIN_BASE_API IAsyncFileSystem
    {
    public:
        virtual ~IAsyncFileSystem() = default;

        virtual AsyncTask<std::unique_ptr<IAsyncFileHandle>> OpenFileAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options) = 0;
        virtual AsyncTask<FileInfo>  GetInfoAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options = {}) = 0;
        virtual AsyncTaskVoid        CopyFileAsync(NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options = {}) = 0;
    };
}// namespace NGIN::IO

#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/IO/AsyncDirectoryHandle.hpp>
#include <NGIN/IO/AsyncFileHandle.hpp>
#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/IOResult.hpp>

namespace NGIN::IO
{
    /// @brief Polymorphic cancellation-aware asynchronous filesystem interface.
    class NGIN_IO_API IAsyncFileSystem
    {
    public:
        /// @brief Destroys the asynchronous filesystem interface.
        virtual ~IAsyncFileSystem() = default;

        /// @brief Asynchronously opens a file with the requested options.
        virtual AsyncTask<AsyncFileHandle> OpenFileAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const FileOpenOptions& options) = 0;
        /// @brief Asynchronously opens a directory.
        virtual AsyncTask<AsyncDirectoryHandle> OpenDirectoryAsync(
                NGIN::Async::TaskContext& ctx, const Path& path) = 0;
        /// @brief Asynchronously queries entry metadata.
        virtual AsyncTask<FileInfo> GetInfoAsync(
                NGIN::Async::TaskContext& ctx, const Path& path, const MetadataOptions& options = {}) = 0;
        /// @brief Asynchronously copies a file according to the supplied policy.
        virtual AsyncTaskVoid CopyFileAsync(NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options = {}) = 0;
    };
}// namespace NGIN::IO

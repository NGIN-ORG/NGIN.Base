#pragma once

#include <NGIN/IO/AsyncFileHandle.hpp>
#include <NGIN/IO/FileSystemTypes.hpp>

#include <memory>

namespace NGIN::IO
{
    class NGIN_BASE_API AsyncDirectoryHandle
    {
    public:
        using ExistsFn = AsyncTask<bool> (*)(
                const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx, const Path& path);
        using GetInfoFn = AsyncTask<FileInfo> (*)(
                const std::shared_ptr<void>& state,
                NGIN::Async::TaskContext&    ctx,
                const Path&                  path,
                const MetadataOptions&       options);
        using OpenFileFn = AsyncTask<AsyncFileHandle> (*)(
                const std::shared_ptr<void>& state,
                NGIN::Async::TaskContext&    ctx,
                const Path&                  path,
                const FileOpenOptions&       options);
        using OpenDirectoryFn = AsyncTask<AsyncDirectoryHandle> (*)(
                const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx, const Path& path);
        using ReadSymlinkFn = AsyncTask<Path> (*)(
                const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx, const Path& path);

        struct Operations
        {
            ExistsFn exists {};
            GetInfoFn getInfo {};
            OpenFileFn openFile {};
            OpenDirectoryFn openDirectory {};
            ReadSymlinkFn readSymlink {};
        };

        AsyncDirectoryHandle() noexcept = default;
        AsyncDirectoryHandle(std::shared_ptr<void> state, const Operations* operations) noexcept
            : m_state(std::move(state))
            , m_operations(operations)
        {
        }

        AsyncDirectoryHandle(const AsyncDirectoryHandle&)                = delete;
        AsyncDirectoryHandle& operator=(const AsyncDirectoryHandle&)     = delete;
        AsyncDirectoryHandle(AsyncDirectoryHandle&&) noexcept            = default;
        AsyncDirectoryHandle& operator=(AsyncDirectoryHandle&&) noexcept = default;
        ~AsyncDirectoryHandle()                                          = default;

        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_state) && m_operations != nullptr; }
        explicit           operator bool() const noexcept { return IsValid(); }

        AsyncTask<bool> ExistsAsync(NGIN::Async::TaskContext& ctx, Path path)
        {
            if (!IsValid() || m_operations->exists == nullptr)
                co_return NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("async directory handle is empty"));
            co_return co_await m_operations->exists(m_state, ctx, path);
        }

        AsyncTask<FileInfo> GetInfoAsync(NGIN::Async::TaskContext& ctx, Path path, MetadataOptions options = {})
        {
            if (!IsValid() || m_operations->getInfo == nullptr)
                co_return NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("async directory handle is empty"));
            co_return co_await m_operations->getInfo(m_state, ctx, path, options);
        }

        AsyncTask<AsyncFileHandle> OpenFileAsync(NGIN::Async::TaskContext& ctx, Path path, FileOpenOptions options)
        {
            if (!IsValid() || m_operations->openFile == nullptr)
                co_return NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("async directory handle is empty"));
            co_return co_await m_operations->openFile(m_state, ctx, path, options);
        }

        AsyncTask<AsyncDirectoryHandle> OpenDirectoryAsync(NGIN::Async::TaskContext& ctx, Path path)
        {
            if (!IsValid() || m_operations->openDirectory == nullptr)
                co_return NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("async directory handle is empty"));
            co_return co_await m_operations->openDirectory(m_state, ctx, path);
        }

        AsyncTask<Path> ReadSymlinkAsync(NGIN::Async::TaskContext& ctx, Path path)
        {
            if (!IsValid() || m_operations->readSymlink == nullptr)
                co_return NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("async directory handle is empty"));
            co_return co_await m_operations->readSymlink(m_state, ctx, path);
        }

    private:
        [[nodiscard]] static IOError MakeInvalidHandleError(const char* message) noexcept
        {
            IOError error;
            error.code    = IOErrorCode::InvalidArgument;
            error.message = message;
            return error;
        }

        std::shared_ptr<void> m_state {};
        const Operations*     m_operations {nullptr};
    };
}// namespace NGIN::IO

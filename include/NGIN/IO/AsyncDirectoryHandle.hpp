#pragma once

#include <NGIN/IO/AsyncFileHandle.hpp>
#include <NGIN/IO/FileSystemTypes.hpp>

#include <memory>

namespace NGIN::IO
{
    /// @brief Move-only type-erased handle for asynchronous directory-relative operations.
    class NGIN_IO_API AsyncDirectoryHandle
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

        /// @brief Function table implemented by an asynchronous directory backend.
        struct Operations
        {
            ExistsFn        exists {};
            GetInfoFn       getInfo {};
            OpenFileFn      openFile {};
            OpenDirectoryFn openDirectory {};
            ReadSymlinkFn   readSymlink {};
        };

        /// @brief Constructs an empty asynchronous directory handle.
        AsyncDirectoryHandle() noexcept = default;
        /// @brief Binds shared backend state to a static operation table.
        /// @note The operation table must outlive this handle and all of its moves.
        AsyncDirectoryHandle(std::shared_ptr<void> state, const Operations* operations) noexcept
            : m_state(std::move(state)), m_operations(operations)
        {
        }

        /// @brief Asynchronous directory handles are non-copyable to keep ownership explicit.
        AsyncDirectoryHandle(const AsyncDirectoryHandle&) = delete;
        /// @brief Asynchronous directory handles are non-copy-assignable to keep ownership explicit.
        AsyncDirectoryHandle& operator=(const AsyncDirectoryHandle&) = delete;
        /// @brief Transfers shared backend state and its operation table.
        AsyncDirectoryHandle(AsyncDirectoryHandle&&) noexcept = default;
        /// @brief Transfers shared backend state and its operation table.
        AsyncDirectoryHandle& operator=(AsyncDirectoryHandle&&) noexcept = default;
        /// @brief Releases this handle's reference to the backend state.
        ~AsyncDirectoryHandle() = default;

        /// @brief Returns whether state and an operation table are both bound.
        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_state) && m_operations != nullptr; }
        /// @brief Returns whether state and an operation table are both bound.
        explicit operator bool() const noexcept { return IsValid(); }

        /// @brief Asynchronously checks whether a relative entry exists.
        AsyncTask<bool> ExistsAsync(NGIN::Async::TaskContext& ctx, Path path)
        {
            if (!IsValid() || m_operations->exists == nullptr)
                co_return MakeInvalidHandleError("async directory handle is empty");
            co_return co_await m_operations->exists(m_state, ctx, path);
        }

        /// @brief Asynchronously queries metadata for a relative entry.
        AsyncTask<FileInfo> GetInfoAsync(NGIN::Async::TaskContext& ctx, Path path, MetadataOptions options = {})
        {
            if (!IsValid() || m_operations->getInfo == nullptr)
                co_return MakeInvalidHandleError("async directory handle is empty");
            co_return co_await m_operations->getInfo(m_state, ctx, path, options);
        }

        /// @brief Asynchronously opens a file relative to this directory.
        AsyncTask<AsyncFileHandle> OpenFileAsync(NGIN::Async::TaskContext& ctx, Path path, FileOpenOptions options)
        {
            if (!IsValid() || m_operations->openFile == nullptr)
                co_return MakeInvalidHandleError("async directory handle is empty");
            co_return co_await m_operations->openFile(m_state, ctx, path, options);
        }

        /// @brief Asynchronously opens a directory relative to this directory.
        AsyncTask<AsyncDirectoryHandle> OpenDirectoryAsync(NGIN::Async::TaskContext& ctx, Path path)
        {
            if (!IsValid() || m_operations->openDirectory == nullptr)
                co_return MakeInvalidHandleError("async directory handle is empty");
            co_return co_await m_operations->openDirectory(m_state, ctx, path);
        }

        /// @brief Asynchronously reads the stored target of a relative symbolic link.
        AsyncTask<Path> ReadSymlinkAsync(NGIN::Async::TaskContext& ctx, Path path)
        {
            if (!IsValid() || m_operations->readSymlink == nullptr)
                co_return MakeInvalidHandleError("async directory handle is empty");
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

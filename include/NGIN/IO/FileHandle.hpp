#pragma once

#include <NGIN/IO/IFileHandle.hpp>

#include <memory>

namespace NGIN::IO
{
    /// @brief Move-only owning facade over an `IFileHandle` implementation.
    class NGIN_IO_API FileHandle
    {
    public:
        /// @brief Constructs an empty handle.
        FileHandle() noexcept = default;
        /// @brief Takes ownership of a concrete file-handle implementation.
        explicit FileHandle(std::unique_ptr<IFileHandle> handle) noexcept
            : m_handle(std::move(handle))
        {
        }

        /// @brief File handles are non-copyable because they uniquely own their implementation.
        FileHandle(const FileHandle&) = delete;
        /// @brief File handles are non-copy-assignable because they uniquely own their implementation.
        FileHandle& operator=(const FileHandle&) = delete;
        /// @brief Transfers ownership from another handle.
        FileHandle(FileHandle&&) noexcept = default;
        /// @brief Transfers ownership from another handle.
        FileHandle& operator=(FileHandle&&) noexcept = default;
        /// @brief Destroys the owned implementation.
        ~FileHandle() = default;

        /// @brief Returns whether this facade contains an implementation.
        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_handle); }
        /// @brief Returns whether this facade contains an implementation.
        explicit operator bool() const noexcept { return IsValid(); }

        /// @brief Reads from the current position and advances it.
        Result<UIntSize> Read(std::span<NGIN::Byte> destination) noexcept
        {
            if (!m_handle)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Read(destination);
        }

        /// @brief Writes at the current position and advances it.
        Result<UIntSize> Write(std::span<const NGIN::Byte> source) noexcept
        {
            if (!m_handle)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Write(source);
        }

        /// @brief Reads at an absolute offset without changing the current position.
        Result<UIntSize> ReadAt(UInt64 offset, std::span<NGIN::Byte> destination) noexcept
        {
            if (!m_handle)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->ReadAt(offset, destination);
        }

        /// @brief Writes at an absolute offset without changing the current position.
        Result<UIntSize> WriteAt(UInt64 offset, std::span<const NGIN::Byte> source) noexcept
        {
            if (!m_handle)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->WriteAt(offset, source);
        }

        /// @brief Flushes buffered contents to storage.
        ResultVoid Flush() noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Flush();
        }

        /// @brief Moves the sequential position relative to an origin.
        ResultVoid Seek(Int64 offset, SeekOrigin origin) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Seek(offset, origin);
        }

        /// @brief Returns the current sequential position.
        Result<UInt64> Tell() const noexcept
        {
            if (!m_handle)
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Tell();
        }

        /// @brief Returns the current file size in bytes.
        Result<UInt64> Size() const noexcept
        {
            if (!m_handle)
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Size();
        }

        /// @brief Truncates or extends the file to the requested size.
        ResultVoid SetSize(UInt64 size) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->SetSize(size);
        }

        /// @brief Closes the underlying file resource when present.
        void Close() noexcept
        {
            if (m_handle)
                m_handle->Close();
        }

        /// @brief Returns whether the underlying implementation reports an open file.
        [[nodiscard]] bool IsOpen() const noexcept
        {
            return m_handle && m_handle->IsOpen();
        }

    private:
        [[nodiscard]] static IOError MakeInvalidHandleError(const char* message) noexcept
        {
            IOError error;
            error.code    = IOErrorCode::InvalidArgument;
            error.message = message;
            return error;
        }

        std::unique_ptr<IFileHandle> m_handle {};
    };
}// namespace NGIN::IO

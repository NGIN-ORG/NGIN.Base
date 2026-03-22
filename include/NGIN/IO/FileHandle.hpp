#pragma once

#include <NGIN/IO/IFileHandle.hpp>

#include <memory>

namespace NGIN::IO
{
    class NGIN_BASE_API FileHandle
    {
    public:
        FileHandle() noexcept = default;
        explicit FileHandle(std::unique_ptr<IFileHandle> handle) noexcept
            : m_handle(std::move(handle))
        {
        }

        FileHandle(const FileHandle&)                = delete;
        FileHandle& operator=(const FileHandle&)     = delete;
        FileHandle(FileHandle&&) noexcept            = default;
        FileHandle& operator=(FileHandle&&) noexcept = default;
        ~FileHandle()                                = default;

        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_handle); }
        explicit           operator bool() const noexcept { return IsValid(); }

        Result<UIntSize> Read(std::span<NGIN::Byte> destination) noexcept
        {
            if (!m_handle)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Read(destination);
        }

        Result<UIntSize> Write(std::span<const NGIN::Byte> source) noexcept
        {
            if (!m_handle)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Write(source);
        }

        Result<UIntSize> ReadAt(UInt64 offset, std::span<NGIN::Byte> destination) noexcept
        {
            if (!m_handle)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->ReadAt(offset, destination);
        }

        Result<UIntSize> WriteAt(UInt64 offset, std::span<const NGIN::Byte> source) noexcept
        {
            if (!m_handle)
                return Result<UIntSize>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->WriteAt(offset, source);
        }

        ResultVoid Flush() noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Flush();
        }

        ResultVoid Seek(Int64 offset, SeekOrigin origin) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Seek(offset, origin);
        }

        Result<UInt64> Tell() const noexcept
        {
            if (!m_handle)
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Tell();
        }

        Result<UInt64> Size() const noexcept
        {
            if (!m_handle)
                return Result<UInt64>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->Size();
        }

        ResultVoid SetSize(UInt64 size) noexcept
        {
            if (!m_handle)
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("file handle is empty")));
            return m_handle->SetSize(size);
        }

        void Close() noexcept
        {
            if (m_handle)
                m_handle->Close();
        }

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

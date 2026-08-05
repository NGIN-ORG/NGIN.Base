#pragma once

#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/IOResult.hpp>

#include <span>

namespace NGIN::IO
{
    /// @brief Polymorphic interface for sequential and positioned file IO.
    class NGIN_IO_API IFileHandle
    {
    public:
        /// @brief Destroys the interface; concrete handles close their resources.
        virtual ~IFileHandle() = default;

        /// @brief Reads from the current position and advances it by the bytes read.
        virtual Result<UIntSize> Read(std::span<NGIN::Byte> destination) noexcept = 0;
        /// @brief Writes at the current position and advances it by the bytes written.
        virtual Result<UIntSize> Write(std::span<const NGIN::Byte> source) noexcept = 0;
        /// @brief Reads at an absolute offset without changing the current position.
        virtual Result<UIntSize> ReadAt(UInt64 offset, std::span<NGIN::Byte> destination) noexcept = 0;
        /// @brief Writes at an absolute offset without changing the current position.
        virtual Result<UIntSize> WriteAt(UInt64 offset, std::span<const NGIN::Byte> source) noexcept = 0;
        /// @brief Flushes buffered file contents to the underlying storage device.
        virtual ResultVoid Flush() noexcept = 0;
        /// @brief Moves the current position relative to the requested origin.
        virtual ResultVoid Seek(Int64 offset, SeekOrigin origin) noexcept = 0;
        /// @brief Returns the current sequential file position.
        virtual Result<UInt64> Tell() const noexcept = 0;
        /// @brief Returns the current file size in bytes.
        virtual Result<UInt64> Size() const noexcept = 0;
        /// @brief Truncates or extends the file to the requested size.
        virtual ResultVoid SetSize(UInt64 size) noexcept = 0;
        /// @brief Closes the handle; calling Close() repeatedly is safe.
        virtual void Close() noexcept = 0;
        /// @brief Returns whether the handle owns an open file resource.
        [[nodiscard]] virtual bool IsOpen() const noexcept = 0;
    };
}// namespace NGIN::IO

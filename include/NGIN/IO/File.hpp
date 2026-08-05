#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/IO/IOError.hpp>
#include <NGIN/IO/Path.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <span>

namespace NGIN::IO
{
    /// @brief Low-level file handle wrapper using platform APIs.
    class NGIN_IO_API File
    {
    public:
        enum class OpenMode : UInt8
        {
            Read,
            Write,
            ReadWrite,
        };

        /// @brief Constructs a closed file.
        File() noexcept = default;
        /// @brief Files are non-copyable because they own platform handles.
        File(const File&) = delete;
        /// @brief Files are non-copy-assignable because they own platform handles.
        File& operator=(const File&) = delete;
        /// @brief Transfers the platform handle from another file.
        File(File&& other) noexcept;
        /// @brief Closes this file and transfers the platform handle from another file.
        File& operator=(File&& other) noexcept;
        /// @brief Closes the file.
        ~File();

        /// @brief Opens a path with the requested access mode.
        NGIN::Utilities::Expected<void, IOError> Open(const Path& path, OpenMode mode) noexcept;
        /// @brief Closes the current platform handle; calling Close() repeatedly is safe.
        void Close() noexcept;

        /// @brief Returns whether the file currently owns an open platform handle.
        [[nodiscard]] bool IsOpen() const noexcept;

        /// @brief Reads up to the destination size from the current file position.
        /// @return The number of bytes read, which may be zero at end of stream.
        NGIN::Utilities::Expected<UIntSize, IOError> Read(std::span<NGIN::Byte> destination) noexcept;
        /// @brief Sets the absolute byte position from the beginning of the file.
        NGIN::Utilities::Expected<void, IOError> Seek(UIntSize offset) noexcept;
        /// @brief Returns the current absolute byte position.
        NGIN::Utilities::Expected<UIntSize, IOError> Tell() const noexcept;
        /// @brief Returns the current file size in bytes.
        NGIN::Utilities::Expected<UIntSize, IOError> Size() const noexcept;

        /// @brief Reads all bytes from the file into an owning buffer.
        NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError> ReadAll() noexcept;

    private:
#if defined(_WIN32)
        void* m_handle {nullptr};
#else
        int m_handle {-1};
#endif
    };
}// namespace NGIN::IO

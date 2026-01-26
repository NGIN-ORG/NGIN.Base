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
    class NGIN_BASE_API File
    {
    public:
        enum class OpenMode : UInt8
        {
            Read,
            Write,
            ReadWrite,
        };

        File() noexcept              = default;
        File(const File&)            = delete;
        File& operator=(const File&) = delete;
        File(File&& other) noexcept;
        File& operator=(File&& other) noexcept;
        ~File();

        NGIN::Utilities::Expected<void, IOError> Open(const Path& path, OpenMode mode) noexcept;
        void                                     Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;

        NGIN::Utilities::Expected<UIntSize, IOError> Read(std::span<NGIN::Byte> destination) noexcept;
        NGIN::Utilities::Expected<void, IOError>     Seek(UIntSize offset) noexcept;
        NGIN::Utilities::Expected<UIntSize, IOError> Tell() const noexcept;
        NGIN::Utilities::Expected<UIntSize, IOError> Size() const noexcept;

        NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError> ReadAll() noexcept;

    private:
#if defined(_WIN32)
        void* m_handle {nullptr};
#else
        int m_handle {-1};
#endif
    };
}// namespace NGIN::IO

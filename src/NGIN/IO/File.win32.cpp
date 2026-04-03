#include <NGIN/IO/File.hpp>

#include <NGIN/Utilities/Expected.hpp>

#include <windows.h>

namespace NGIN::IO
{
    namespace
    {
        [[nodiscard]] IOError MakeSystemError(const char* message, int code) noexcept
        {
            IOError error;
            error.code       = IOErrorCode::SystemError;
            error.systemCode = code;
            error.message    = message ? message : "system error";
            return error;
        }

        [[nodiscard]] IOError MakeInvalidArgumentError(const char* message) noexcept
        {
            IOError error;
            error.code    = IOErrorCode::InvalidArgument;
            error.message = message ? message : "invalid argument";
            return error;
        }
    }// namespace

    File::File(File&& other) noexcept
    {
        *this = std::move(other);
    }

    File& File::operator=(File&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_handle       = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    File::~File()
    {
        Close();
    }

    NGIN::Utilities::Expected<void, IOError> File::Open(const Path& path, OpenMode mode) noexcept
    {
        Close();

        DWORD access   = 0;
        DWORD creation = OPEN_EXISTING;
        switch (mode)
        {
            case OpenMode::Read:
                access   = GENERIC_READ;
                creation = OPEN_EXISTING;
                break;
            case OpenMode::Write:
                access   = GENERIC_WRITE;
                creation = CREATE_ALWAYS;
                break;
            case OpenMode::ReadWrite:
                access   = GENERIC_READ | GENERIC_WRITE;
                creation = OPEN_ALWAYS;
                break;
        }

        HANDLE handle = ::CreateFileA(path.String().CStr(), access, FILE_SHARE_READ, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("CreateFileA failed", static_cast<int>(::GetLastError())));
        }

        m_handle = handle;
        return {};
    }

    void File::Close() noexcept
    {
        if (m_handle != nullptr)
        {
            ::CloseHandle(static_cast<HANDLE>(m_handle));
            m_handle = nullptr;
        }
    }

    bool File::IsOpen() const noexcept
    {
        return m_handle != nullptr;
    }

    NGIN::Utilities::Expected<UIntSize, IOError> File::Read(std::span<NGIN::Byte> destination) noexcept
    {
        if (!IsOpen())
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeInvalidArgumentError("file not open"));
        }
        if (destination.empty())
        {
            return UIntSize {0};
        }

        DWORD bytesRead = 0;
        if (!::ReadFile(static_cast<HANDLE>(m_handle), destination.data(), static_cast<DWORD>(destination.size()), &bytesRead, nullptr))
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("ReadFile failed", static_cast<int>(::GetLastError())));
        }
        return static_cast<UIntSize>(bytesRead);
    }

    NGIN::Utilities::Expected<void, IOError> File::Seek(UIntSize offset) noexcept
    {
        if (!IsOpen())
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeInvalidArgumentError("file not open"));
        }

        LARGE_INTEGER value;
        value.QuadPart = static_cast<LONGLONG>(offset);
        if (!::SetFilePointerEx(static_cast<HANDLE>(m_handle), value, nullptr, FILE_BEGIN))
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("SetFilePointerEx failed", static_cast<int>(::GetLastError())));
        }
        return {};
    }

    NGIN::Utilities::Expected<UIntSize, IOError> File::Tell() const noexcept
    {
        if (!IsOpen())
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeInvalidArgumentError("file not open"));
        }

        LARGE_INTEGER zero {};
        LARGE_INTEGER position {};
        if (!::SetFilePointerEx(static_cast<HANDLE>(m_handle), zero, &position, FILE_CURRENT))
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("SetFilePointerEx failed", static_cast<int>(::GetLastError())));
        }
        return static_cast<UIntSize>(position.QuadPart);
    }

    NGIN::Utilities::Expected<UIntSize, IOError> File::Size() const noexcept
    {
        if (!IsOpen())
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeInvalidArgumentError("file not open"));
        }

        LARGE_INTEGER size;
        if (!::GetFileSizeEx(static_cast<HANDLE>(m_handle), &size))
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("GetFileSizeEx failed", static_cast<int>(::GetLastError())));
        }
        return static_cast<UIntSize>(size.QuadPart);
    }

    NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError> File::ReadAll() noexcept
    {
        auto sizeResult = Size();
        if (!sizeResult.HasValue())
        {
            return NGIN::Utilities::Unexpected<IOError>(std::move(sizeResult.Error()));
        }

        const UIntSize fileSize = sizeResult.Value();
        NGIN::Containers::Vector<NGIN::Byte> data;
        data.Reserve(fileSize);

        static constexpr UIntSize chunkSize = 64 * 1024;
        NGIN::Byte                temp[chunkSize];
        UIntSize                  total = 0;
        while (total < fileSize)
        {
            const UIntSize remaining  = fileSize - total;
            const UIntSize toRead     = remaining < chunkSize ? remaining : chunkSize;
            auto           readResult = Read(std::span<NGIN::Byte>(temp, toRead));
            if (!readResult.HasValue())
            {
                return NGIN::Utilities::Unexpected<IOError>(std::move(readResult.Error()));
            }

            const UIntSize readBytes = readResult.Value();
            if (readBytes == 0)
            {
                break;
            }
            for (UIntSize i = 0; i < readBytes; ++i)
            {
                data.PushBack(temp[i]);
            }
            total += readBytes;
        }

        return data;
    }
}// namespace NGIN::IO

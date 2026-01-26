#include <NGIN/IO/File.hpp>

#include <NGIN/Utilities/Expected.hpp>

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace NGIN::IO
{
    namespace
    {
        [[nodiscard]] IOError MakeSystemError(const char* message, int code) noexcept
        {
            IOError err;
            err.code       = IOErrorCode::SystemError;
            err.systemCode = code;
            err.message    = message ? message : "system error";
            return err;
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
#if defined(_WIN32)
            m_handle       = other.m_handle;
            other.m_handle = nullptr;
#else
            m_handle       = other.m_handle;
            other.m_handle = -1;
#endif
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
#if defined(_WIN32)
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
        HANDLE handle = CreateFileA(path.String().CStr(), access, FILE_SHARE_READ, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("CreateFileA failed", static_cast<int>(GetLastError()))));
        }
        m_handle = handle;
#else
        int flags = 0;
        switch (mode)
        {
            case OpenMode::Read:
                flags = O_RDONLY;
                break;
            case OpenMode::Write:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case OpenMode::ReadWrite:
                flags = O_RDWR | O_CREAT;
                break;
        }
        const int fd = ::open(path.String().CStr(), flags, 0644);
        if (fd < 0)
        {
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("open failed", errno)));
        }
        m_handle = fd;
#endif
        return {};
    }

    void File::Close() noexcept
    {
#if defined(_WIN32)
        if (m_handle)
        {
            CloseHandle(static_cast<HANDLE>(m_handle));
            m_handle = nullptr;
        }
#else
        if (m_handle >= 0)
        {
            ::close(m_handle);
            m_handle = -1;
        }
#endif
    }

    bool File::IsOpen() const noexcept
    {
#if defined(_WIN32)
        return m_handle != nullptr;
#else
        return m_handle >= 0;
#endif
    }

    NGIN::Utilities::Expected<UIntSize, IOError> File::Read(std::span<NGIN::Byte> destination) noexcept
    {
        if (!IsOpen())
        {
            IOError err;
            err.code    = IOErrorCode::InvalidArgument;
            err.message = "file not open";
            return NGIN::Utilities::Expected<UIntSize, IOError>(NGIN::Utilities::Unexpected<IOError>(std::move(err)));
        }
        if (destination.empty())
            return NGIN::Utilities::Expected<UIntSize, IOError>(UIntSize {0});
#if defined(_WIN32)
        DWORD bytesRead = 0;
        if (!ReadFile(static_cast<HANDLE>(m_handle), destination.data(), static_cast<DWORD>(destination.size()), &bytesRead, nullptr))
        {
            return NGIN::Utilities::Expected<UIntSize, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("ReadFile failed", static_cast<int>(GetLastError()))));
        }
        return NGIN::Utilities::Expected<UIntSize, IOError>(static_cast<UIntSize>(bytesRead));
#else
        const ssize_t result = ::read(m_handle, destination.data(), destination.size());
        if (result < 0)
        {
            return NGIN::Utilities::Expected<UIntSize, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("read failed", errno)));
        }
        return NGIN::Utilities::Expected<UIntSize, IOError>(static_cast<UIntSize>(result));
#endif
    }

    NGIN::Utilities::Expected<void, IOError> File::Seek(UIntSize offset) noexcept
    {
        if (!IsOpen())
        {
            IOError err;
            err.code    = IOErrorCode::InvalidArgument;
            err.message = "file not open";
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(std::move(err)));
        }
#if defined(_WIN32)
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(static_cast<HANDLE>(m_handle), li, nullptr, FILE_BEGIN))
        {
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("SetFilePointerEx failed", static_cast<int>(GetLastError()))));
        }
#else
        if (lseek(m_handle, static_cast<off_t>(offset), SEEK_SET) == static_cast<off_t>(-1))
        {
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("lseek failed", errno)));
        }
#endif
        return {};
    }

    NGIN::Utilities::Expected<UIntSize, IOError> File::Tell() const noexcept
    {
        if (!IsOpen())
        {
            IOError err;
            err.code    = IOErrorCode::InvalidArgument;
            err.message = "file not open";
            return NGIN::Utilities::Expected<UIntSize, IOError>(NGIN::Utilities::Unexpected<IOError>(std::move(err)));
        }
#if defined(_WIN32)
        LARGE_INTEGER zero {};
        LARGE_INTEGER pos {};
        if (!SetFilePointerEx(static_cast<HANDLE>(m_handle), zero, &pos, FILE_CURRENT))
        {
            return NGIN::Utilities::Expected<UIntSize, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("SetFilePointerEx failed", static_cast<int>(GetLastError()))));
        }
        return NGIN::Utilities::Expected<UIntSize, IOError>(static_cast<UIntSize>(pos.QuadPart));
#else
        const off_t pos = lseek(m_handle, 0, SEEK_CUR);
        if (pos == static_cast<off_t>(-1))
        {
            return NGIN::Utilities::Expected<UIntSize, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("lseek failed", errno)));
        }
        return NGIN::Utilities::Expected<UIntSize, IOError>(static_cast<UIntSize>(pos));
#endif
    }

    NGIN::Utilities::Expected<UIntSize, IOError> File::Size() const noexcept
    {
        if (!IsOpen())
        {
            IOError err;
            err.code    = IOErrorCode::InvalidArgument;
            err.message = "file not open";
            return NGIN::Utilities::Expected<UIntSize, IOError>(NGIN::Utilities::Unexpected<IOError>(std::move(err)));
        }
#if defined(_WIN32)
        LARGE_INTEGER size;
        if (!GetFileSizeEx(static_cast<HANDLE>(m_handle), &size))
        {
            return NGIN::Utilities::Expected<UIntSize, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("GetFileSizeEx failed", static_cast<int>(GetLastError()))));
        }
        return NGIN::Utilities::Expected<UIntSize, IOError>(static_cast<UIntSize>(size.QuadPart));
#else
        struct stat st;
        if (fstat(m_handle, &st) != 0)
        {
            return NGIN::Utilities::Expected<UIntSize, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("fstat failed", errno)));
        }
        return NGIN::Utilities::Expected<UIntSize, IOError>(static_cast<UIntSize>(st.st_size));
#endif
    }

    NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError> File::ReadAll() noexcept
    {
        auto sizeResult = Size();
        if (!sizeResult.HasValue())
            return NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError>(
                    NGIN::Utilities::Unexpected<IOError>(std::move(sizeResult.ErrorUnsafe())));
        const UIntSize fileSize = sizeResult.ValueUnsafe();

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
                return NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError>(
                        NGIN::Utilities::Unexpected<IOError>(std::move(readResult.ErrorUnsafe())));
            }
            const UIntSize readBytes = readResult.ValueUnsafe();
            if (readBytes == 0)
                break;
            for (UIntSize i = 0; i < readBytes; ++i)
                data.PushBack(temp[i]);
            total += readBytes;
        }

        return NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError>(std::move(data));
    }

}// namespace NGIN::IO

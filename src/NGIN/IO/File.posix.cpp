#include <NGIN/IO/File.hpp>

#include <NGIN/Utilities/Expected.hpp>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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
            other.m_handle = -1;
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

        const int fileDescriptor = ::open(path.String().CStr(), flags, 0644);
        if (fileDescriptor < 0)
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("open failed", errno));
        }

        m_handle = fileDescriptor;
        return {};
    }

    void File::Close() noexcept
    {
        if (m_handle >= 0)
        {
            ::close(m_handle);
            m_handle = -1;
        }
    }

    bool File::IsOpen() const noexcept
    {
        return m_handle >= 0;
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

        const ssize_t result = ::read(m_handle, destination.data(), destination.size());
        if (result < 0)
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("read failed", errno));
        }
        return static_cast<UIntSize>(result);
    }

    NGIN::Utilities::Expected<void, IOError> File::Seek(UIntSize offset) noexcept
    {
        if (!IsOpen())
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeInvalidArgumentError("file not open"));
        }

        if (::lseek(m_handle, static_cast<off_t>(offset), SEEK_SET) == static_cast<off_t>(-1))
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("lseek failed", errno));
        }
        return {};
    }

    NGIN::Utilities::Expected<UIntSize, IOError> File::Tell() const noexcept
    {
        if (!IsOpen())
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeInvalidArgumentError("file not open"));
        }

        const off_t position = ::lseek(m_handle, 0, SEEK_CUR);
        if (position == static_cast<off_t>(-1))
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("lseek failed", errno));
        }
        return static_cast<UIntSize>(position);
    }

    NGIN::Utilities::Expected<UIntSize, IOError> File::Size() const noexcept
    {
        if (!IsOpen())
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeInvalidArgumentError("file not open"));
        }

        struct stat information;
        if (::fstat(m_handle, &information) != 0)
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("fstat failed", errno));
        }
        return static_cast<UIntSize>(information.st_size);
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

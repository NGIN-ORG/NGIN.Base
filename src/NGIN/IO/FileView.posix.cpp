#include <NGIN/IO/FileView.hpp>

#include <cerrno>
#include <fcntl.h>
#include <new>
#include <stdexcept>
#include <sys/mman.h>
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

        [[nodiscard]] NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError>
        ReadBuffered(const int fileDescriptor, const UIntSize fileSize) noexcept
        {
            try
            {
                NGIN::Containers::Vector<NGIN::Byte> buffer;
                buffer.Reserve(fileSize);

                static constexpr UIntSize chunkSize = 64 * 1024;
                NGIN::Byte                chunk[chunkSize];
                UIntSize                  totalRead = 0;
                while (totalRead < fileSize)
                {
                    const UIntSize bytesRemaining = fileSize - totalRead;
                    const UIntSize bytesToRead    = bytesRemaining < chunkSize ? bytesRemaining : chunkSize;

                    ssize_t readCount = 0;
                    do
                    {
                        readCount = ::read(fileDescriptor, chunk, bytesToRead);
                    } while (readCount < 0 && errno == EINTR);

                    if (readCount < 0)
                    {
                        return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("buffered read failed", errno));
                    }
                    if (readCount == 0)
                        break;

                    const UIntSize bytesRead = static_cast<UIntSize>(readCount);
                    for (UIntSize index = 0; index < bytesRead; ++index)
                        buffer.PushBack(chunk[index]);
                    totalRead += bytesRead;
                }
                return buffer;
            } catch (const std::bad_alloc&)
            {
                return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("buffer allocation failed", ENOMEM));
            } catch (const std::length_error&)
            {
                return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("file is too large to buffer", EOVERFLOW));
            }
        }
    }// namespace

    FileView::FileView(FileView&& other) noexcept
    {
        *this = std::move(other);
    }

    FileView& FileView::operator=(FileView&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_fileHandle       = other.m_fileHandle;
            other.m_fileHandle = -1;
            m_data             = other.m_data;
            m_size             = other.m_size;
            m_ownsBuffer       = other.m_ownsBuffer;
            m_buffer           = std::move(other.m_buffer);
            other.m_data       = nullptr;
            other.m_size       = 0;
            other.m_ownsBuffer = false;
        }
        return *this;
    }

    FileView::~FileView()
    {
        Close();
    }

    NGIN::Utilities::Expected<void, IOError> FileView::Open(const Path& path) noexcept
    {
        Close();

        const int fileDescriptor = ::open(path.String().CStr(), O_RDONLY);
        if (fileDescriptor < 0)
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("open failed", errno));
        }

        struct stat information;
        if (::fstat(fileDescriptor, &information) != 0)
        {
            ::close(fileDescriptor);
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("fstat failed", errno));
        }

        if (information.st_size == 0)
        {
            m_fileHandle = fileDescriptor;
            m_size       = 0;
            return {};
        }

        void* view = ::mmap(nullptr, static_cast<size_t>(information.st_size), PROT_READ, MAP_PRIVATE, fileDescriptor, 0);
        if (view != MAP_FAILED)
        {
            m_fileHandle = fileDescriptor;
            m_data       = static_cast<const NGIN::Byte*>(view);
            m_size       = static_cast<UIntSize>(information.st_size);
            m_ownsBuffer = false;
            return {};
        }

        // Mapping can fail because of address-space or platform limits even
        // though ordinary reads remain available. Reuse the same descriptor so
        // the fallback still refers to the file that was inspected above.
        NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError> readResult =
                ReadBuffered(fileDescriptor, static_cast<UIntSize>(information.st_size));
        ::close(fileDescriptor);
        if (!readResult.has_value())
        {
            return NGIN::Utilities::Unexpected<IOError>(std::move(readResult.error()));
        }

        m_buffer     = std::move(readResult.value());
        m_data       = m_buffer.data();
        m_size       = m_buffer.Size();
        m_ownsBuffer = true;
        return {};
    }

    void FileView::Close() noexcept
    {
        if (m_ownsBuffer)
        {
            m_buffer     = NGIN::Containers::Vector<NGIN::Byte> {};
            m_ownsBuffer = false;
        }

        if (m_data != nullptr && m_fileHandle >= 0 && !m_ownsBuffer)
        {
            ::munmap(const_cast<NGIN::Byte*>(m_data), m_size);
        }
        if (m_fileHandle >= 0)
        {
            ::close(m_fileHandle);
        }

        m_fileHandle = -1;
        m_data       = nullptr;
        m_size       = 0;
    }

    bool FileView::IsOpen() const noexcept
    {
        return m_fileHandle >= 0 || m_ownsBuffer;
    }

    std::span<const NGIN::Byte> FileView::Data() const noexcept
    {
        return std::span<const NGIN::Byte>(m_data, m_size);
    }
}// namespace NGIN::IO

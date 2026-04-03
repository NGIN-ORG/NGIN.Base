#include <NGIN/IO/FileView.hpp>

#include <NGIN/IO/File.hpp>

#include <cerrno>
#include <fcntl.h>
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

        ::close(fileDescriptor);

        File file;
        auto openResult = file.Open(path, File::OpenMode::Read);
        if (!openResult.HasValue())
        {
            return openResult;
        }

        auto readResult = file.ReadAll();
        file.Close();
        if (!readResult.HasValue())
        {
            return NGIN::Utilities::Unexpected<IOError>(std::move(readResult.Error()));
        }

        m_buffer     = std::move(readResult.Value());
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

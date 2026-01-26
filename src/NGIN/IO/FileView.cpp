#include <NGIN/IO/FileView.hpp>

#include <NGIN/IO/File.hpp>

#include <cerrno>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
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

    FileView::FileView(FileView&& other) noexcept
    {
        *this = std::move(other);
    }

    FileView& FileView::operator=(FileView&& other) noexcept
    {
        if (this != &other)
        {
            Close();
#if defined(_WIN32)
            m_fileHandle          = other.m_fileHandle;
            m_mappingHandle       = other.m_mappingHandle;
            other.m_fileHandle    = nullptr;
            other.m_mappingHandle = nullptr;
#else
            m_fileHandle       = other.m_fileHandle;
            other.m_fileHandle = -1;
#endif
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

#if defined(_WIN32)
        HANDLE handle = CreateFileA(path.String().CStr(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("CreateFileA failed", static_cast<int>(GetLastError()))));
        }
        LARGE_INTEGER size;
        if (!GetFileSizeEx(handle, &size))
        {
            CloseHandle(handle);
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("GetFileSizeEx failed", static_cast<int>(GetLastError()))));
        }
        if (size.QuadPart == 0)
        {
            m_fileHandle = handle;
            m_size       = 0;
            return {};
        }
        HANDLE mapping = CreateFileMappingA(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping)
        {
            CloseHandle(handle);
        }
        else
        {
            void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
            if (view)
            {
                m_fileHandle    = handle;
                m_mappingHandle = mapping;
                m_data          = static_cast<const NGIN::Byte*>(view);
                m_size          = static_cast<UIntSize>(size.QuadPart);
                m_ownsBuffer    = false;
                return {};
            }
            CloseHandle(mapping);
            CloseHandle(handle);
        }
#else
        const int fd = ::open(path.String().CStr(), O_RDONLY);
        if (fd < 0)
        {
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("open failed", errno)));
        }
        struct stat st;
        if (fstat(fd, &st) != 0)
        {
            ::close(fd);
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(
                    MakeSystemError("fstat failed", errno)));
        }
        if (st.st_size == 0)
        {
            m_fileHandle = fd;
            m_size       = 0;
            return {};
        }
        void* view = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
        if (view != MAP_FAILED)
        {
            m_fileHandle = fd;
            m_data       = static_cast<const NGIN::Byte*>(view);
            m_size       = static_cast<UIntSize>(st.st_size);
            m_ownsBuffer = false;
            return {};
        }
        ::close(fd);
#endif

        File file;
        auto openResult = file.Open(path, File::OpenMode::Read);
        if (!openResult.HasValue())
            return openResult;
        auto readResult = file.ReadAll();
        file.Close();
        if (!readResult.HasValue())
            return NGIN::Utilities::Expected<void, IOError>(NGIN::Utilities::Unexpected<IOError>(std::move(readResult.ErrorUnsafe())));
        m_buffer     = std::move(readResult.ValueUnsafe());
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
#if defined(_WIN32)
        if (m_data && m_mappingHandle)
            UnmapViewOfFile(m_data);
        if (m_mappingHandle)
            CloseHandle(static_cast<HANDLE>(m_mappingHandle));
        if (m_fileHandle)
            CloseHandle(static_cast<HANDLE>(m_fileHandle));
        m_fileHandle    = nullptr;
        m_mappingHandle = nullptr;
#else
        if (m_data && m_fileHandle >= 0 && !m_ownsBuffer)
            munmap(const_cast<NGIN::Byte*>(m_data), m_size);
        if (m_fileHandle >= 0)
            ::close(m_fileHandle);
        m_fileHandle = -1;
#endif
        m_data = nullptr;
        m_size = 0;
    }

    bool FileView::IsOpen() const noexcept
    {
#if defined(_WIN32)
        return m_fileHandle != nullptr || m_ownsBuffer;
#else
        return m_fileHandle >= 0 || m_ownsBuffer;
#endif
    }

    std::span<const NGIN::Byte> FileView::Data() const noexcept
    {
        return std::span<const NGIN::Byte>(m_data, m_size);
    }

}// namespace NGIN::IO

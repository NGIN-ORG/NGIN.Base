#include <NGIN/IO/FileView.hpp>

#include <new>
#include <stdexcept>
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

        [[nodiscard]] NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError>
        ReadBuffered(const HANDLE fileHandle, const UIntSize fileSize) noexcept
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
                    const DWORD    bytesToRead    = static_cast<DWORD>(bytesRemaining < chunkSize ? bytesRemaining : chunkSize);
                    DWORD          bytesRead      = 0;
                    if (!::ReadFile(fileHandle, chunk, bytesToRead, &bytesRead, nullptr))
                    {
                        return NGIN::Utilities::Unexpected<IOError>(
                                MakeSystemError("buffered ReadFile failed", static_cast<int>(::GetLastError())));
                    }
                    if (bytesRead == 0)
                        break;

                    for (DWORD index = 0; index < bytesRead; ++index)
                        buffer.PushBack(chunk[index]);
                    totalRead += static_cast<UIntSize>(bytesRead);
                }
                return buffer;
            } catch (const std::bad_alloc&)
            {
                return NGIN::Utilities::Unexpected<IOError>(
                        MakeSystemError("buffer allocation failed", static_cast<int>(ERROR_NOT_ENOUGH_MEMORY)));
            } catch (const std::length_error&)
            {
                return NGIN::Utilities::Unexpected<IOError>(
                        MakeSystemError("file is too large to buffer", static_cast<int>(ERROR_FILE_TOO_LARGE)));
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
            m_fileHandle          = other.m_fileHandle;
            m_mappingHandle       = other.m_mappingHandle;
            other.m_fileHandle    = nullptr;
            other.m_mappingHandle = nullptr;
            m_data                = other.m_data;
            m_size                = other.m_size;
            m_ownsBuffer          = other.m_ownsBuffer;
            m_buffer              = std::move(other.m_buffer);
            other.m_data          = nullptr;
            other.m_size          = 0;
            other.m_ownsBuffer    = false;
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

        HANDLE handle = ::CreateFileA(path.String().CStr(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("CreateFileA failed", static_cast<int>(::GetLastError())));
        }

        LARGE_INTEGER size;
        if (!::GetFileSizeEx(handle, &size))
        {
            ::CloseHandle(handle);
            return NGIN::Utilities::Unexpected<IOError>(MakeSystemError("GetFileSizeEx failed", static_cast<int>(::GetLastError())));
        }

        if (size.QuadPart == 0)
        {
            m_fileHandle = handle;
            m_size       = 0;
            return {};
        }

        HANDLE mapping = ::CreateFileMappingA(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping != nullptr)
        {
            void* view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
            if (view != nullptr)
            {
                m_fileHandle    = handle;
                m_mappingHandle = mapping;
                m_data          = static_cast<const NGIN::Byte*>(view);
                m_size          = static_cast<UIntSize>(size.QuadPart);
                m_ownsBuffer    = false;
                return {};
            }
            ::CloseHandle(mapping);
        }
        // Mapping can fail because of address-space or platform limits even
        // though ordinary reads remain available. Reuse the same handle so the
        // fallback still refers to the file that was inspected above.
        NGIN::Utilities::Expected<NGIN::Containers::Vector<NGIN::Byte>, IOError> readResult =
                ReadBuffered(handle, static_cast<UIntSize>(size.QuadPart));
        ::CloseHandle(handle);
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

        if (m_data != nullptr && m_mappingHandle != nullptr)
        {
            ::UnmapViewOfFile(m_data);
        }
        if (m_mappingHandle != nullptr)
        {
            ::CloseHandle(static_cast<HANDLE>(m_mappingHandle));
        }
        if (m_fileHandle != nullptr)
        {
            ::CloseHandle(static_cast<HANDLE>(m_fileHandle));
        }

        m_fileHandle    = nullptr;
        m_mappingHandle = nullptr;
        m_data          = nullptr;
        m_size          = 0;
    }

    bool FileView::IsOpen() const noexcept
    {
        return m_fileHandle != nullptr || m_ownsBuffer;
    }

    std::span<const NGIN::Byte> FileView::Data() const noexcept
    {
        return std::span<const NGIN::Byte>(m_data, m_size);
    }
}// namespace NGIN::IO

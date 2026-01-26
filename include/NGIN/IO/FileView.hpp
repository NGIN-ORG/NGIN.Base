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
    /// @brief Read-only file mapping with fallback to buffered load.
    class NGIN_BASE_API FileView
    {
    public:
        FileView() noexcept                  = default;
        FileView(const FileView&)            = delete;
        FileView& operator=(const FileView&) = delete;
        FileView(FileView&& other) noexcept;
        FileView& operator=(FileView&& other) noexcept;
        ~FileView();

        NGIN::Utilities::Expected<void, IOError> Open(const Path& path) noexcept;
        void                                     Close() noexcept;

        [[nodiscard]] bool                        IsOpen() const noexcept;
        [[nodiscard]] std::span<const NGIN::Byte> Data() const noexcept;
        [[nodiscard]] UIntSize                    Size() const noexcept { return m_size; }

    private:
#if defined(_WIN32)
        void* m_fileHandle {nullptr};
        void* m_mappingHandle {nullptr};
#else
        int m_fileHandle {-1};
#endif
        const NGIN::Byte*                    m_data {nullptr};
        UIntSize                             m_size {0};
        bool                                 m_ownsBuffer {false};
        NGIN::Containers::Vector<NGIN::Byte> m_buffer {};
    };
}// namespace NGIN::IO

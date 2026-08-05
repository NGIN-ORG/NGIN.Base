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
    class NGIN_IO_API FileView
    {
    public:
        /// @brief Constructs a closed file view.
        FileView() noexcept = default;
        /// @brief File views are non-copyable because they own platform mapping resources.
        FileView(const FileView&) = delete;
        /// @brief File views are non-copy-assignable because they own platform mapping resources.
        FileView& operator=(const FileView&) = delete;
        /// @brief Transfers the mapping and fallback buffer from another view.
        FileView(FileView&& other) noexcept;
        /// @brief Closes this view and transfers resources from another view.
        FileView& operator=(FileView&& other) noexcept;
        /// @brief Closes the view and releases its mapping or fallback buffer.
        ~FileView();

        /// @brief Opens a read-only view of a file, using a buffered fallback when mapping is unavailable.
        NGIN::Utilities::Expected<void, IOError> Open(const Path& path) noexcept;
        /// @brief Closes the view and invalidates spans returned by Data().
        void Close() noexcept;

        /// @brief Returns whether the view currently owns mapped or buffered file data.
        [[nodiscard]] bool IsOpen() const noexcept;
        /// @brief Returns a non-owning span valid until Close(), move assignment, or destruction.
        [[nodiscard]] std::span<const NGIN::Byte> Data() const noexcept;
        /// @brief Returns the number of bytes in the view.
        [[nodiscard]] UIntSize Size() const noexcept { return m_size; }

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

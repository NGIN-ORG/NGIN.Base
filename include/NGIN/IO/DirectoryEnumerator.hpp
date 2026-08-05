#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/IO/IDirectoryEnumerator.hpp>

#include <memory>

namespace NGIN::IO
{
    /// @brief Move-only owning facade over an incremental directory enumerator.
    class NGIN_IO_API DirectoryEnumerator
    {
    public:
        /// @brief Constructs an empty enumerator.
        DirectoryEnumerator() noexcept = default;
        /// @brief Takes ownership of a concrete enumerator implementation.
        explicit DirectoryEnumerator(std::unique_ptr<IDirectoryEnumerator> enumerator) noexcept
            : m_enumerator(std::move(enumerator))
        {
        }

        /// @brief Enumerators are non-copyable because they uniquely own traversal state.
        DirectoryEnumerator(const DirectoryEnumerator&) = delete;
        /// @brief Enumerators are non-copy-assignable because they uniquely own traversal state.
        DirectoryEnumerator& operator=(const DirectoryEnumerator&) = delete;
        /// @brief Transfers traversal ownership from another enumerator.
        DirectoryEnumerator(DirectoryEnumerator&&) noexcept = default;
        /// @brief Transfers traversal ownership from another enumerator.
        DirectoryEnumerator& operator=(DirectoryEnumerator&&) noexcept = default;
        /// @brief Destroys the owned traversal state.
        ~DirectoryEnumerator() = default;

        /// @brief Returns whether this facade contains an enumerator implementation.
        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_enumerator); }
        /// @brief Returns whether this facade contains an enumerator implementation.
        explicit operator bool() const noexcept { return IsValid(); }

        /// @brief Returns the next entry, or a successful empty result at end of enumeration.
        Result<DirectoryEnumerationNext> Next() noexcept
        {
            if (!m_enumerator)
                return Result<DirectoryEnumerationNext>(NGIN::Utilities::Unexpected<IOError>(MakeInvalidEnumeratorError()));
            return m_enumerator->Next();
        }

    private:
        [[nodiscard]] static IOError MakeInvalidEnumeratorError() noexcept
        {
            IOError error;
            error.code    = IOErrorCode::InvalidArgument;
            error.message = "directory enumerator is empty";
            return error;
        }

        std::unique_ptr<IDirectoryEnumerator> m_enumerator {};
    };
}// namespace NGIN::IO

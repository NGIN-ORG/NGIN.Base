#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/IO/IDirectoryEnumerator.hpp>

#include <memory>

namespace NGIN::IO
{
    class NGIN_BASE_API DirectoryEnumerator
    {
    public:
        DirectoryEnumerator() noexcept = default;
        explicit DirectoryEnumerator(std::unique_ptr<IDirectoryEnumerator> enumerator) noexcept
            : m_enumerator(std::move(enumerator))
        {
        }

        DirectoryEnumerator(const DirectoryEnumerator&)                = delete;
        DirectoryEnumerator& operator=(const DirectoryEnumerator&)     = delete;
        DirectoryEnumerator(DirectoryEnumerator&&) noexcept            = default;
        DirectoryEnumerator& operator=(DirectoryEnumerator&&) noexcept = default;
        ~DirectoryEnumerator()                                         = default;

        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_enumerator); }
        explicit           operator bool() const noexcept { return IsValid(); }

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

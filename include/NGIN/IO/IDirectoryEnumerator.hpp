#pragma once

#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/IOResult.hpp>

namespace NGIN::IO
{
    /// @brief Polymorphic incremental directory-enumeration interface.
    class NGIN_IO_API IDirectoryEnumerator
    {
    public:
        /// @brief Destroys the enumerator and releases its traversal state.
        virtual ~IDirectoryEnumerator() = default;

        /// @brief Returns the next entry, or a successful empty result at end of enumeration.
        virtual Result<DirectoryEnumerationNext> Next() noexcept = 0;
    };
}// namespace NGIN::IO

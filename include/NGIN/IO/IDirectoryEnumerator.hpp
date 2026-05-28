#pragma once

#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/IOResult.hpp>

namespace NGIN::IO
{
    class NGIN_BASE_API IDirectoryEnumerator
    {
    public:
        virtual ~IDirectoryEnumerator() = default;

        virtual Result<DirectoryEnumerationNext> Next() noexcept = 0;
    };
}// namespace NGIN::IO

#include "NativeFileSystemBackend.hpp"

#if !defined(__linux__) && !defined(NGIN_PLATFORM_WINDOWS)

namespace NGIN::IO::detail
{
    std::shared_ptr<NativeFileBackend> CreateNativeFileBackend(FileSystemDriver&)
    {
        return {};
    }
}// namespace NGIN::IO::detail

#endif

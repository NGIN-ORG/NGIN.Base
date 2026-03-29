#include "NativeFileSystemBackend.hpp"

#if !defined(__linux__) && !defined(NGIN_PLATFORM_WINDOWS)

namespace NGIN::IO::detail
{
    std::unique_ptr<NativeFileBackend> CreateNativeFileBackend(const FileSystemDriver::Options&)
    {
        return {};
    }
}

#endif

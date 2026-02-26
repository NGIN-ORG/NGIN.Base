#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/IO/Path.hpp>
#include <NGIN/Primitives.hpp>

#include <optional>

namespace NGIN::IO
{
    enum class EntryType : UInt8
    {
        None,
        File,
        Directory,
        Symlink,
        Other,
    };

    enum class SeekOrigin : UInt8
    {
        Begin,
        Current,
        End,
    };

    enum class FileAccess : UInt8
    {
        Read,
        Write,
        ReadWrite,
        Append,
    };

    enum class FileShare : UInt8
    {
        None  = 0,
        Read  = 1u << 0u,
        Write = 1u << 1u,
        Delete = 1u << 2u,
        All   = Read | Write | Delete,
    };

    constexpr FileShare operator|(FileShare a, FileShare b) noexcept
    {
        return static_cast<FileShare>(static_cast<UInt8>(a) | static_cast<UInt8>(b));
    }
    constexpr FileShare operator&(FileShare a, FileShare b) noexcept
    {
        return static_cast<FileShare>(static_cast<UInt8>(a) & static_cast<UInt8>(b));
    }
    constexpr bool Any(FileShare v) noexcept
    {
        return static_cast<UInt8>(v) != 0;
    }

    enum class FileCreateDisposition : UInt8
    {
        OpenExisting,
        CreateAlways,
        CreateNew,
        OpenAlways,
        TruncateExisting,
    };

    enum class FileOpenFlags : UInt16
    {
        None         = 0,
        Sequential   = 1u << 0u,
        RandomAccess = 1u << 1u,
        WriteThrough = 1u << 2u,
        Temporary    = 1u << 3u,
        DeleteOnClose = 1u << 4u,
        AsyncPreferred = 1u << 5u,
    };

    constexpr FileOpenFlags operator|(FileOpenFlags a, FileOpenFlags b) noexcept
    {
        return static_cast<FileOpenFlags>(static_cast<UInt16>(a) | static_cast<UInt16>(b));
    }
    constexpr FileOpenFlags operator&(FileOpenFlags a, FileOpenFlags b) noexcept
    {
        return static_cast<FileOpenFlags>(static_cast<UInt16>(a) & static_cast<UInt16>(b));
    }
    constexpr bool Any(FileOpenFlags v) noexcept
    {
        return static_cast<UInt16>(v) != 0;
    }

    struct FileOpenOptions
    {
        FileAccess            access {FileAccess::Read};
        FileShare             share {FileShare::Read};
        FileCreateDisposition disposition {FileCreateDisposition::OpenExisting};
        FileOpenFlags         flags {FileOpenFlags::None};
    };

    struct CopyOptions
    {
        bool overwriteExisting {false};
        bool recursive {false};
    };

    struct RemoveOptions
    {
        bool recursive {false};
        bool ignoreMissing {false};
    };

    struct DirectoryCreateOptions
    {
        bool recursive {false};
        bool ignoreIfExists {true};
    };

    struct EnumerateOptions
    {
        bool recursive {false};
        bool includeFiles {true};
        bool includeDirectories {true};
        bool includeSymlinks {false};
        bool followSymlinks {false};
        bool stableSort {false};
    };

    struct FilePermissions
    {
        UInt32 nativeBits {0};
        bool   readable {false};
        bool   writable {false};
        bool   executable {false};
    };

    struct FileTime
    {
        Int64 unixNanoseconds {0};
        bool  valid {false};
    };

    struct FileInfo
    {
        Path            path {};
        EntryType       type {EntryType::None};
        UInt64          size {0};
        FileTime        created {};
        FileTime        modified {};
        FileTime        accessed {};
        FilePermissions permissions {};
        bool            exists {false};
    };

    struct SpaceInfo
    {
        UInt64 capacity {0};
        UInt64 free {0};
        UInt64 available {0};
    };

    struct DirectoryEntry
    {
        Path                    path {};
        Path                    name {};
        EntryType               type {EntryType::None};
        std::optional<FileInfo> info {};
    };
}// namespace NGIN::IO


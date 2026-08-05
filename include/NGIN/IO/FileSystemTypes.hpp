#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/IO/Path.hpp>
#include <NGIN/Primitives.hpp>

#include <optional>
#include <utility>

namespace NGIN::IO
{
    /// @brief Portable kind of filesystem entry.
    enum class EntryType : UInt8
    {
        Unknown,
        File,
        Directory,
        Symlink,
        BlockDevice,
        CharacterDevice,
        Fifo,
        Socket,
        Other,
    };

    /// @brief Controls whether metadata operations follow symbolic links.
    enum class SymlinkMode : UInt8
    {
        DoNotFollow,
        Follow,
    };

    /// @brief Reference position used by file seek operations.
    enum class SeekOrigin : UInt8
    {
        Begin,
        Current,
        End,
    };

    /// @brief Access requested when opening a file.
    enum class FileAccess : UInt8
    {
        Read,
        Write,
        ReadWrite,
        Append,
    };

    /// @brief Bit mask describing access allowed to other open handles.
    enum class FileShare : UInt8
    {
        None   = 0,
        Read   = 1u << 0u,
        Write  = 1u << 1u,
        Delete = 1u << 2u,
        All    = Read | Write | Delete,
    };

    /// @brief Combines file-sharing flags.
    constexpr FileShare operator|(FileShare a, FileShare b) noexcept
    {
        return static_cast<FileShare>(static_cast<UInt8>(a) | static_cast<UInt8>(b));
    }
    /// @brief Intersects file-sharing flags.
    constexpr FileShare operator&(FileShare a, FileShare b) noexcept
    {
        return static_cast<FileShare>(static_cast<UInt8>(a) & static_cast<UInt8>(b));
    }
    /// @brief Returns whether any file-sharing flag is set.
    constexpr bool Any(FileShare v) noexcept
    {
        return static_cast<UInt8>(v) != 0;
    }

    /// @brief Existing-file behavior requested when opening a path.
    enum class FileCreateDisposition : UInt8
    {
        OpenExisting,
        CreateAlways,
        CreateNew,
        OpenAlways,
        TruncateExisting,
    };

    /// @brief Optional platform-independent file-open hints and behaviors.
    enum class FileOpenFlags : UInt16
    {
        None           = 0,
        Sequential     = 1u << 0u,
        RandomAccess   = 1u << 1u,
        WriteThrough   = 1u << 2u,
        Temporary      = 1u << 3u,
        DeleteOnClose  = 1u << 4u,
        AsyncPreferred = 1u << 5u,
    };

    /// @brief Combines file-open flags.
    constexpr FileOpenFlags operator|(FileOpenFlags a, FileOpenFlags b) noexcept
    {
        return static_cast<FileOpenFlags>(static_cast<UInt16>(a) | static_cast<UInt16>(b));
    }
    /// @brief Intersects file-open flags.
    constexpr FileOpenFlags operator&(FileOpenFlags a, FileOpenFlags b) noexcept
    {
        return static_cast<FileOpenFlags>(static_cast<UInt16>(a) & static_cast<UInt16>(b));
    }
    /// @brief Returns whether any file-open flag is set.
    constexpr bool Any(FileOpenFlags v) noexcept
    {
        return static_cast<UInt16>(v) != 0;
    }

    /// @brief Options controlling file access, sharing, creation, and platform hints.
    struct FileOpenOptions
    {
        FileAccess            access {FileAccess::Read};
        FileShare             share {FileShare::Read};
        FileCreateDisposition disposition {FileCreateDisposition::OpenExisting};
        FileOpenFlags         flags {FileOpenFlags::None};
    };

    /// @brief Symbolic-link policy for copy operations.
    enum class CopySymlinkMode : UInt8
    {
        Preserve,
        Follow,
        Reject,
    };

    /// @brief Options controlling file and directory copies.
    struct CopyOptions
    {
        bool            overwriteExisting {false};
        bool            recursive {false};
        CopySymlinkMode symlinks {CopySymlinkMode::Preserve};
        bool            preservePermissions {true};
        bool            cleanupOnFailure {true};
    };

    /// @brief Durability options for atomic file replacement.
    struct ReplaceOptions
    {
        /// Flush the source file contents before the atomic name replacement.
        bool flushSource {false};
        /// Flush the destination parent directory after replacement.
        bool flushParentDirectory {false};
    };

    /// @brief Options controlling file or directory removal.
    struct RemoveOptions
    {
        bool recursive {false};
        bool ignoreMissing {false};
    };

    /// @brief Options controlling directory creation.
    struct DirectoryCreateOptions
    {
        bool recursive {false};
        bool ignoreIfExists {true};
    };

    /// @brief Ordering applied to directory enumeration results.
    enum class DirectorySortOrder : UInt8
    {
        Unspecified,
        LexicalPath,
        LexicalName,
    };

    /// @brief Filtering, recursion, metadata, and ordering options for enumeration.
    struct EnumerateOptions
    {
        bool               recursive {false};
        bool               includeFiles {true};
        bool               includeDirectories {true};
        bool               includeSymlinks {false};
        bool               followSymlinks {false};
        bool               populateInfo {false};
        DirectorySortOrder sortOrder {DirectorySortOrder::Unspecified};
    };

    /// @brief Options controlling filesystem metadata queries.
    struct MetadataOptions
    {
        SymlinkMode symlinkMode {SymlinkMode::DoNotFollow};
    };

    /// @brief Portable permission summary together with native permission bits.
    struct FilePermissions
    {
        UInt32 nativeBits {0};
        bool   readable {false};
        bool   writable {false};
        bool   executable {false};
        bool   setUserId {false};
        bool   setGroupId {false};
        bool   sticky {false};
    };

    /// @brief Portable numeric ownership metadata when available.
    struct FileOwnership
    {
        UInt32 userId {0};
        UInt32 groupId {0};
        bool   valid {false};
    };

    /// @brief Stable filesystem identity and hard-link metadata when available.
    struct FileIdentity
    {
        UInt64 device {0};
        UInt64 inode {0};
        UInt64 hardLinkCount {0};
        bool   valid {false};
    };

    /// @brief Optional filesystem timestamp expressed as Unix nanoseconds.
    struct FileTime
    {
        Int64 unixNanoseconds {0};
        bool  valid {false};
    };

    /// @brief Metadata describing a filesystem entry.
    struct FileInfo
    {
        Path            path {};
        EntryType       type {EntryType::Unknown};
        UInt64          size {0};
        FileTime        created {};
        FileTime        modified {};
        FileTime        accessed {};
        FileTime        changed {};
        FilePermissions permissions {};
        FileOwnership   ownership {};
        FileIdentity    identity {};
        bool            exists {false};
        bool            symlinkTargetExists {false};
    };

    /// @brief Capacity and availability information for a filesystem volume.
    struct SpaceInfo
    {
        UInt64 capacity {0};
        UInt64 free {0};
        UInt64 available {0};
    };

    /// @brief One enumerated directory entry with optional populated metadata.
    struct DirectoryEntry
    {
        Path                    path {};
        Path                    name {};
        EntryType               type {EntryType::Unknown};
        std::optional<FileInfo> info {};
    };

    /// @brief Optional next item returned by incremental directory enumerators.
    class DirectoryEnumerationNext
    {
    public:
        /// @brief Constructs an end-of-enumeration result.
        DirectoryEnumerationNext() noexcept = default;

        /// @brief Constructs a result containing an entry.
        explicit DirectoryEnumerationNext(DirectoryEntry entry)
            : m_hasEntry(true), m_entry(std::move(entry))
        {
        }

        /// @brief Returns whether this result contains an entry.
        [[nodiscard]] bool HasEntry() const noexcept { return m_hasEntry; }
        /// @brief Returns whether this result contains an entry.
        explicit operator bool() const noexcept { return HasEntry(); }

        /// @brief Returns the contained entry.
        /// @pre HasEntry() is true; otherwise the process aborts.
        [[nodiscard]] const DirectoryEntry& Entry() const noexcept
        {
            if (!m_hasEntry)
            {
                NGIN_ASSERT(false && "NGIN::IO::DirectoryEnumerationNext::Entry called without an entry");
                NGIN_ABORT("NGIN::IO::DirectoryEnumerationNext::Entry called without an entry");
            }
            return m_entry;
        }

        /// @brief Returns the mutable contained entry.
        /// @pre HasEntry() is true; otherwise the process aborts.
        [[nodiscard]] DirectoryEntry& Entry() noexcept
        {
            if (!m_hasEntry)
            {
                NGIN_ASSERT(false && "NGIN::IO::DirectoryEnumerationNext::Entry called without an entry");
                NGIN_ABORT("NGIN::IO::DirectoryEnumerationNext::Entry called without an entry");
            }
            return m_entry;
        }

    private:
        bool           m_hasEntry {false};
        DirectoryEntry m_entry {};
    };

    /// @brief Feature set supported by a filesystem implementation.
    struct FileSystemCapabilities
    {
        bool symlinks {false};
        bool hardLinks {false};
        bool blockDevices {false};
        bool characterDevices {false};
        bool fifos {false};
        bool sockets {false};
        bool posixModeBits {false};
        bool ownership {false};
        bool setIdBits {false};
        bool stickyBit {false};
        bool fileIdentity {false};
        bool hardLinkCount {false};
        bool memoryMappedFiles {false};
        bool nanosecondTimestamps {false};
        bool metadataNoFollow {false};
        bool atomicRenameNoReplace {false};
        bool atomicReplace {false};
        bool durableReplace {false};
    };
}// namespace NGIN::IO

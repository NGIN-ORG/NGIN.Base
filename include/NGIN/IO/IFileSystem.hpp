#pragma once

#include <NGIN/IO/DirectoryEnumerator.hpp>
#include <NGIN/IO/DirectoryHandle.hpp>
#include <NGIN/IO/FileHandle.hpp>
#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/FileView.hpp>
#include <NGIN/IO/IOResult.hpp>
#include <string_view>

namespace NGIN::IO
{
    /// @brief Complete synchronous filesystem abstraction using portable IO errors.
    class NGIN_IO_API IFileSystem
    {
    public:
        /// @brief Destroys the filesystem interface.
        virtual ~IFileSystem() = default;

        /// @brief Returns the optional features supported by this implementation.
        [[nodiscard]] virtual FileSystemCapabilities GetCapabilities() const noexcept = 0;
        /// @brief Returns whether an entry exists at a path.
        virtual Result<bool> Exists(const Path& path) noexcept = 0;
        /// @brief Returns metadata for an entry according to the symbolic-link policy.
        virtual Result<FileInfo> GetInfo(const Path& path, const MetadataOptions& options = {}) noexcept = 0;
        /// @brief Resolves a path to absolute lexical form using @p base or the working directory.
        virtual Result<Path> Absolute(const Path& path, const Path& base = {}) noexcept = 0;
        /// @brief Resolves a path to canonical form; all components must exist.
        virtual Result<Path> Canonical(const Path& path) noexcept = 0;
        /// @brief Canonicalizes the existing prefix and appends any non-existing suffix lexically.
        virtual Result<Path> WeaklyCanonical(const Path& path) noexcept = 0;
        /// @brief Returns whether two paths identify the same filesystem object.
        virtual Result<bool> SameFile(const Path& lhs, const Path& rhs) noexcept = 0;
        /// @brief Reads the stored target of a symbolic link without following it.
        virtual Result<Path> ReadSymlink(const Path& path) noexcept = 0;

        /// @brief Creates one directory according to the supplied existence policy.
        virtual ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept = 0;
        /// @brief Creates a directory and any missing parents.
        virtual ResultVoid CreateDirectories(const Path& path, const DirectoryCreateOptions& options = {}) noexcept = 0;
        /// @brief Creates a symbolic link at @p linkPath storing @p target.
        virtual ResultVoid CreateSymlink(const Path& target, const Path& linkPath) noexcept = 0;
        /// @brief Creates a hard link at @p linkPath to the existing target.
        virtual ResultVoid CreateHardLink(const Path& target, const Path& linkPath) noexcept = 0;
        /// @brief Replaces permission metadata, optionally following symbolic links.
        virtual ResultVoid SetPermissions(const Path& path, const FilePermissions& permissions, const SymlinkMode symlinkMode = SymlinkMode::Follow) noexcept = 0;
        /// @brief Removes a file according to the supplied missing-entry policy.
        virtual ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept = 0;
        /// @brief Removes a directory according to the supplied recursion and missing-entry policy.
        virtual ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept = 0;
        /// @brief Recursively removes an entry tree and returns the number of entries removed.
        virtual Result<UInt64> RemoveAll(const Path& path, const RemoveOptions& options = {}) noexcept = 0;

        /// @brief Renames an entry, replacing the destination where supported.
        virtual ResultVoid Rename(const Path& from, const Path& to) noexcept = 0;
        /// @brief Atomically renames an entry only when the destination does not exist.
        virtual ResultVoid RenameNoReplace(const Path& from, const Path& to) noexcept = 0;
        /// @brief Atomically replaces a destination file and optionally requests durability flushes.
        virtual ResultVoid ReplaceFile(const Path& source, const Path& destination, const ReplaceOptions& options = {}) noexcept = 0;
        /// @brief Copies a file or entry tree according to the supplied policy.
        virtual ResultVoid CopyFile(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept = 0;
        /// @brief Moves an entry, using copy policy when a direct rename is unavailable.
        virtual ResultVoid Move(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept = 0;

        /// @brief Opens a file and returns its owning handle.
        virtual Result<FileHandle> OpenFile(const Path& path, const FileOpenOptions& options) noexcept = 0;
        /// @brief Opens a directory and returns its owning handle.
        virtual Result<DirectoryHandle> OpenDirectory(const Path& path) noexcept = 0;
        /// @brief Opens a read-only mapped or buffered file view.
        virtual Result<FileView> OpenFileView(const Path& path) noexcept = 0;
        /// @brief Creates an incremental enumerator for a directory tree.
        virtual Result<DirectoryEnumerator> Enumerate(const Path& path, const EnumerateOptions& options = {}) noexcept = 0;

        /// @brief Returns the process working directory.
        virtual Result<Path> CurrentWorkingDirectory() noexcept = 0;
        /// @brief Sets the process working directory.
        virtual ResultVoid SetCurrentWorkingDirectory(const Path& path) noexcept = 0;
        /// @brief Returns the platform temporary-file directory.
        virtual Result<Path> TempDirectory() noexcept = 0;
        /// @brief Creates a uniquely named temporary directory.
        virtual Result<Path> CreateTempDirectory(const Path& directory = {}, std::string_view prefix = "ngin") noexcept = 0;
        /// @brief Creates a uniquely named empty temporary file.
        virtual Result<Path> CreateTempFile(const Path& directory = {}, std::string_view prefix = "ngin") noexcept = 0;
        /// @brief Returns volume capacity and available-space information for a path.
        virtual Result<SpaceInfo> GetSpaceInfo(const Path& path) noexcept = 0;
    };
}// namespace NGIN::IO

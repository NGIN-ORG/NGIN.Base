#pragma once

#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/FileView.hpp>
#include <NGIN/IO/IDirectoryEnumerator.hpp>
#include <NGIN/IO/IFileHandle.hpp>
#include <NGIN/IO/IOResult.hpp>

#include <memory>

namespace NGIN::IO
{
    class NGIN_BASE_API IFileSystem
    {
    public:
        virtual ~IFileSystem() = default;

        virtual Result<bool> Exists(const Path& path) noexcept = 0;
        virtual Result<FileInfo> GetInfo(const Path& path) noexcept = 0;

        virtual ResultVoid CreateDirectory(const Path& path, const DirectoryCreateOptions& options = {}) noexcept = 0;
        virtual ResultVoid CreateDirectories(const Path& path, const DirectoryCreateOptions& options = {}) noexcept = 0;
        virtual ResultVoid RemoveFile(const Path& path, const RemoveOptions& options = {}) noexcept = 0;
        virtual ResultVoid RemoveDirectory(const Path& path, const RemoveOptions& options = {}) noexcept = 0;
        virtual Result<UInt64> RemoveAll(const Path& path, const RemoveOptions& options = {}) noexcept = 0;

        virtual ResultVoid Rename(const Path& from, const Path& to) noexcept = 0;
        virtual ResultVoid CopyFile(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept = 0;
        virtual ResultVoid Move(const Path& from, const Path& to, const CopyOptions& options = {}) noexcept = 0;

        virtual Result<std::unique_ptr<IFileHandle>> OpenFile(const Path& path, const FileOpenOptions& options) noexcept = 0;
        virtual Result<FileView> OpenFileView(const Path& path) noexcept = 0;
        virtual Result<std::unique_ptr<IDirectoryEnumerator>> Enumerate(const Path& path, const EnumerateOptions& options = {}) noexcept = 0;

        virtual Result<Path> CurrentWorkingDirectory() noexcept = 0;
        virtual ResultVoid   SetCurrentWorkingDirectory(const Path& path) noexcept = 0;
        virtual Result<Path> TempDirectory() noexcept = 0;
        virtual Result<SpaceInfo> GetSpaceInfo(const Path& path) noexcept = 0;
    };
}// namespace NGIN::IO


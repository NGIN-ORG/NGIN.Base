#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/IO/IAsyncFileSystem.hpp>
#include <NGIN/IO/IFileSystem.hpp>
#include <NGIN/Text/String.hpp>

namespace NGIN::IO
{
    NGIN_BASE_API Result<NGIN::Containers::Vector<NGIN::Byte>> ReadAllBytes(IFileSystem& fs, const Path& path) noexcept;
    NGIN_BASE_API Result<NGIN::Text::String>                   ReadAllText(IFileSystem& fs, const Path& path) noexcept;
    NGIN_BASE_API ResultVoid                                   WriteAllBytes(IFileSystem& fs, const Path& path, std::span<const NGIN::Byte> bytes) noexcept;
    NGIN_BASE_API ResultVoid                                   WriteAllText(IFileSystem& fs, const Path& path, std::string_view text) noexcept;
    NGIN_BASE_API ResultVoid                                   AppendAllText(IFileSystem& fs, const Path& path, std::string_view text) noexcept;
    NGIN_BASE_API ResultVoid                                   EnsureDirectory(IFileSystem& fs, const Path& path) noexcept;

    NGIN_BASE_API AsyncTask<NGIN::Containers::Vector<NGIN::Byte>> ReadAllBytesAsync(
            IAsyncFileSystem& fs, NGIN::Async::TaskContext& ctx, const Path& path);
    NGIN_BASE_API AsyncTaskVoid WriteAllBytesAsync(
            IAsyncFileSystem& fs, NGIN::Async::TaskContext& ctx, const Path& path, std::span<const NGIN::Byte> bytes);
    NGIN_BASE_API AsyncTaskVoid CopyFileAsync(
            IAsyncFileSystem& fs, NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options = {});
}// namespace NGIN::IO


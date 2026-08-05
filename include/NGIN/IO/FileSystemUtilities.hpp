/// @file FileSystemUtilities.hpp
/// @brief Whole-file, atomic-write, directory, and asynchronous filesystem convenience operations.
#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/IO/AtomicWriteOptions.hpp>
#include <NGIN/IO/IAsyncFileSystem.hpp>
#include <NGIN/IO/IFileSystem.hpp>
#include <NGIN/Text/String.hpp>

#include <string_view>

namespace NGIN::IO
{
    /// @brief Reads an entire file into a byte vector.
    NGIN_IO_API Result<NGIN::Containers::Vector<NGIN::Byte>> ReadAllBytes(IFileSystem& fs, const Path& path) noexcept;
    /// @brief Reads an entire file into a UTF-8 string.
    NGIN_IO_API Result<NGIN::Text::String> ReadAllText(IFileSystem& fs, const Path& path) noexcept;
    /// @brief Replaces a file with the supplied bytes.
    NGIN_IO_API ResultVoid WriteAllBytes(IFileSystem& fs, const Path& path, std::span<const NGIN::Byte> bytes) noexcept;
    /// @brief Replaces a file with the supplied UTF-8 text.
    NGIN_IO_API ResultVoid WriteAllText(IFileSystem& fs, const Path& path, std::string_view text) noexcept;
    /// @brief Writes bytes to a sibling temporary file and atomically replaces the destination.
    NGIN_IO_API ResultVoid WriteAllBytesAtomic(
            IFileSystem& fs, const Path& path, std::span<const NGIN::Byte> bytes, const AtomicWriteOptions& options = {}) noexcept;
    /// @brief Writes text to a sibling temporary file and atomically replaces the destination.
    NGIN_IO_API ResultVoid WriteAllTextAtomic(
            IFileSystem& fs, const Path& path, std::string_view text, const AtomicWriteOptions& options = {}) noexcept;
    /// @brief Appends UTF-8 text to a file, creating it when necessary.
    NGIN_IO_API ResultVoid AppendAllText(IFileSystem& fs, const Path& path, std::string_view text) noexcept;
    /// @brief Ensures that a directory and its missing ancestors exist.
    NGIN_IO_API ResultVoid EnsureDirectory(IFileSystem& fs, const Path& path) noexcept;

    /// @brief Asynchronously reads an entire file into a byte vector.
    NGIN_IO_API AsyncTask<NGIN::Containers::Vector<NGIN::Byte>> ReadAllBytesAsync(
            IAsyncFileSystem& fs, NGIN::Async::TaskContext& ctx, const Path& path);
    /// @brief Asynchronously replaces a file with the supplied bytes.
    NGIN_IO_API AsyncTaskVoid WriteAllBytesAsync(
            IAsyncFileSystem& fs, NGIN::Async::TaskContext& ctx, const Path& path, std::span<const NGIN::Byte> bytes);
    /// @brief Asynchronously copies one file to another filesystem path.
    NGIN_IO_API AsyncTaskVoid CopyFileAsync(
            IAsyncFileSystem& fs, NGIN::Async::TaskContext& ctx, const Path& from, const Path& to, const CopyOptions& options = {});
}// namespace NGIN::IO

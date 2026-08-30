#include <NGIN/IO/FileSystemUtilities.hpp>

#include <NGIN/Utilities/Expected.hpp>

#include <chrono>
#include <string>
#include <string_view>

namespace NGIN::IO
{
    namespace
    {
        [[nodiscard]] IOError MakeError(IOErrorCode code, std::string_view message, const Path& path = {}) noexcept
        {
            IOError error;
            error.code    = code;
            error.path    = path;
            error.message = message;
            return error;
        }

        [[nodiscard]] Path MakeAtomicTempPath(const Path& path, const AtomicWriteOptions& options, const UInt64 uniqueValue, const UIntSize attempt)
        {
            std::string name;
            name.append(options.tempPrefix);
            const auto filename = path.Filename();
            name.append(filename.empty() ? "file" : filename);
            name.push_back('.');
            name.append(std::to_string(uniqueValue));
            name.push_back('.');
            name.append(std::to_string(attempt));

            const auto parent = path.Parent();
            if (parent.IsEmpty())
                return Path {name};
            return parent.Join(name);
        }

        ResultVoid WriteAllBytesToOpenFile(FileHandle& file, const Path& path, std::span<const NGIN::Byte> bytes) noexcept
        {
            UIntSize total = 0;
            while (total < bytes.size())
            {
                auto write = file.Write(bytes.subspan(total));
                if (!write.has_value())
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(write.error())));
                const UIntSize n = write.value();
                if (n == 0)
                    return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::SystemError, "short write", path)));
                total += n;
            }
            return {};
        }
    }// namespace

    Result<NGIN::Containers::Vector<NGIN::Byte>> ReadAllBytes(IFileSystem& fs, const Path& path) noexcept
    {
        FileOpenOptions options;
        options.access      = FileAccess::Read;
        options.disposition = FileCreateDisposition::OpenExisting;

        auto fileResult = fs.OpenFile(path, options);
        if (!fileResult.has_value())
            return Result<NGIN::Containers::Vector<NGIN::Byte>>(NGIN::Utilities::Unexpected<IOError>(std::move(fileResult.error())));

        auto sizeResult = fileResult->Size();
        if (!sizeResult.has_value())
            return Result<NGIN::Containers::Vector<NGIN::Byte>>(NGIN::Utilities::Unexpected<IOError>(std::move(sizeResult.error())));

        NGIN::Containers::Vector<NGIN::Byte> bytes;
        const auto                           fileSize = static_cast<UIntSize>(sizeResult.value());
        if (fileSize > 0)
            bytes.Reserve(fileSize);

        NGIN::Byte temp[64 * 1024];
        for (;;)
        {
            auto read = fileResult->Read(std::span<NGIN::Byte>(temp, sizeof(temp)));
            if (!read.has_value())
                return Result<NGIN::Containers::Vector<NGIN::Byte>>(NGIN::Utilities::Unexpected<IOError>(std::move(read.error())));
            const UIntSize n = read.value();
            if (n == 0)
                break;
            for (UIntSize i = 0; i < n; ++i)
                bytes.PushBack(temp[i]);
        }

        return Result<NGIN::Containers::Vector<NGIN::Byte>>(std::move(bytes));
    }

    Result<NGIN::Text::String> ReadAllText(IFileSystem& fs, const Path& path) noexcept
    {
        auto bytes = ReadAllBytes(fs, path);
        if (!bytes.has_value())
            return Result<NGIN::Text::String>(NGIN::Utilities::Unexpected<IOError>(std::move(bytes.error())));

        NGIN::Text::String text;
        auto&              data = bytes.value();
        if (data.Size() > 0)
            text.Append(std::string_view(reinterpret_cast<const char*>(data.data()), data.Size()));
        return Result<NGIN::Text::String>(std::move(text));
    }

    ResultVoid WriteAllBytes(IFileSystem& fs, const Path& path, std::span<const NGIN::Byte> bytes) noexcept
    {
        FileOpenOptions options;
        options.access      = FileAccess::Write;
        options.disposition = FileCreateDisposition::CreateAlways;

        auto fileResult = fs.OpenFile(path, options);
        if (!fileResult.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fileResult.error())));

        auto wrote = WriteAllBytesToOpenFile(fileResult.value(), path, bytes);
        if (!wrote.has_value())
            return wrote;
        return fileResult->Flush();
    }

    ResultVoid WriteAllText(IFileSystem& fs, const Path& path, std::string_view text) noexcept
    {
        return WriteAllBytes(fs, path, std::span<const NGIN::Byte>(reinterpret_cast<const NGIN::Byte*>(text.data()), text.size()));
    }

    ResultVoid WriteAllBytesAtomic(IFileSystem& fs, const Path& path, std::span<const NGIN::Byte> bytes, const AtomicWriteOptions& options) noexcept
    {
        if (options.createParentDirectories)
        {
            const auto parent = path.Parent();
            if (!parent.IsEmpty() && parent.View() != path.View())
            {
                auto directory = EnsureDirectory(fs, parent);
                if (!directory.has_value())
                    return directory;
            }
        }

        FileOpenOptions openOptions;
        openOptions.access      = FileAccess::Write;
        openOptions.disposition = FileCreateDisposition::CreateNew;
        openOptions.flags       = options.bestEffortDurable ? FileOpenFlags::WriteThrough : FileOpenFlags::None;

        const auto uniqueValue = static_cast<UInt64>(std::chrono::steady_clock::now().time_since_epoch().count());

        IOError lastAlreadyExistsError;
        for (UIntSize attempt = 0; attempt < 64; ++attempt)
        {
            const auto tempPath = MakeAtomicTempPath(path, options, uniqueValue, attempt);
            auto       file     = fs.OpenFile(tempPath, openOptions);
            if (!file.has_value())
            {
                if (file.error().code == IOErrorCode::AlreadyExists)
                {
                    lastAlreadyExistsError = std::move(file.error());
                    continue;
                }
                return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(file.error())));
            }

            auto wrote = WriteAllBytesToOpenFile(file.value(), tempPath, bytes);
            if (!wrote.has_value())
            {
                file->Close();
                RemoveOptions removeOptions;
                removeOptions.ignoreMissing = true;
                (void) fs.RemoveFile(tempPath, removeOptions);
                return wrote;
            }

            if (options.bestEffortDurable)
            {
                auto flushed = file->Flush();
                if (!flushed.has_value())
                {
                    file->Close();
                    RemoveOptions removeOptions;
                    removeOptions.ignoreMissing = true;
                    (void) fs.RemoveFile(tempPath, removeOptions);
                    return flushed;
                }
            }

            file->Close();

            auto replaced = fs.ReplaceFile(tempPath, path);
            if (!replaced.has_value())
            {
                RemoveOptions removeOptions;
                removeOptions.ignoreMissing = true;
                (void) fs.RemoveFile(tempPath, removeOptions);
                return replaced;
            }

            return {};
        }

        if (lastAlreadyExistsError.code != IOErrorCode::None)
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(lastAlreadyExistsError)));
        return ResultVoid(NGIN::Utilities::Unexpected<IOError>(MakeError(IOErrorCode::AlreadyExists, "could not create atomic temp file", path)));
    }

    ResultVoid WriteAllTextAtomic(IFileSystem& fs, const Path& path, std::string_view text, const AtomicWriteOptions& options) noexcept
    {
        return WriteAllBytesAtomic(
                fs, path, std::span<const NGIN::Byte>(reinterpret_cast<const NGIN::Byte*>(text.data()), text.size()), options);
    }

    ResultVoid AppendAllText(IFileSystem& fs, const Path& path, std::string_view text) noexcept
    {
        FileOpenOptions options;
        options.access      = FileAccess::Append;
        options.disposition = FileCreateDisposition::OpenAlways;

        auto fileResult = fs.OpenFile(path, options);
        if (!fileResult.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(fileResult.error())));
        auto write = fileResult->Write(std::span<const NGIN::Byte>(reinterpret_cast<const NGIN::Byte*>(text.data()), text.size()));
        if (!write.has_value())
            return ResultVoid(NGIN::Utilities::Unexpected<IOError>(std::move(write.error())));
        return fileResult->Flush();
    }

    ResultVoid EnsureDirectory(IFileSystem& fs, const Path& path) noexcept
    {
        DirectoryCreateOptions options;
        options.recursive      = true;
        options.ignoreIfExists = true;
        return fs.CreateDirectories(path, options);
    }

    AsyncTask<NGIN::Containers::Vector<NGIN::Byte>> ReadAllBytesAsync(IAsyncFileSystem& fs, NGIN::Async::TaskContext& ctx, Path path)
    {
        FileOpenOptions options;
        options.access      = FileAccess::Read;
        options.share       = FileShare::All;
        options.disposition = FileCreateDisposition::OpenExisting;

        auto file = co_await fs.OpenFileAsync(ctx, std::move(path), options);

        NGIN::Containers::Vector<NGIN::Byte> bytes;
        NGIN::Byte                           temp[64 * 1024];
        for (;;)
        {
            const UIntSize n = co_await file.ReadAsync(ctx, std::span<NGIN::Byte>(temp, sizeof(temp)));
            if (n == 0)
                break;
            for (UIntSize i = 0; i < n; ++i)
                bytes.PushBack(temp[i]);
        }

        co_return std::move(bytes);
    }

    AsyncTaskVoid WriteAllBytesAsync(IAsyncFileSystem& fs, NGIN::Async::TaskContext& ctx, Path path, std::span<const NGIN::Byte> bytes)
    {
        FileOpenOptions options;
        options.access      = FileAccess::Write;
        options.share       = FileShare::All;
        options.disposition = FileCreateDisposition::CreateAlways;

        auto file = co_await fs.OpenFileAsync(ctx, path, options);

        UIntSize total = 0;
        while (total < bytes.size())
        {
            const UIntSize n = co_await file.WriteAsync(ctx, bytes.subspan(total));
            if (n == 0)
            {
                co_await NGIN::Async::DomainFailure(MakeError(IOErrorCode::SystemError, "short write", path));
                co_return;
            }
            total += n;
        }

        co_await file.FlushAsync(ctx);
        co_return;
    }

    AsyncTaskVoid CopyFileAsync(IAsyncFileSystem& fs, NGIN::Async::TaskContext& ctx, Path from, Path to, CopyOptions options)
    {
        co_await fs.CopyFileAsync(ctx, std::move(from), std::move(to), std::move(options));
        co_return;
    }
}// namespace NGIN::IO

#pragma once

#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/IOResult.hpp>

#include <span>

namespace NGIN::IO
{
    class NGIN_BASE_API IFileHandle
    {
    public:
        virtual ~IFileHandle() = default;

        virtual Result<UIntSize> Read(std::span<NGIN::Byte> destination) noexcept = 0;
        virtual Result<UIntSize> Write(std::span<const NGIN::Byte> source) noexcept = 0;
        virtual Result<UIntSize> ReadAt(UInt64 offset, std::span<NGIN::Byte> destination) noexcept = 0;
        virtual Result<UIntSize> WriteAt(UInt64 offset, std::span<const NGIN::Byte> source) noexcept = 0;
        virtual ResultVoid       Flush() noexcept = 0;
        virtual ResultVoid       Seek(Int64 offset, SeekOrigin origin) noexcept = 0;
        virtual Result<UInt64>   Tell() const noexcept = 0;
        virtual Result<UInt64>   Size() const noexcept = 0;
        virtual ResultVoid       SetSize(UInt64 size) noexcept = 0;
        virtual void             Close() noexcept = 0;
        [[nodiscard]] virtual bool IsOpen() const noexcept = 0;
    };
}// namespace NGIN::IO


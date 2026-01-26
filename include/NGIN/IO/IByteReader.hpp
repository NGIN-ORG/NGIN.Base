#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/IO/IOError.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <span>

namespace NGIN::IO
{
    /// @brief Minimal byte reader interface for streaming inputs.
    class NGIN_BASE_API IByteReader
    {
    public:
        virtual ~IByteReader() = default;

        /// @brief Read up to destination.size() bytes into destination.
        virtual NGIN::Utilities::Expected<UIntSize, IOError> Read(std::span<NGIN::Byte> destination) noexcept = 0;

        /// @brief Skip forward by up to @p bytes.
        virtual NGIN::Utilities::Expected<UIntSize, IOError> Skip(UIntSize bytes) noexcept = 0;

        /// @brief Peek up to destination.size() bytes without advancing.
        virtual NGIN::Utilities::Expected<UIntSize, IOError> Peek(std::span<NGIN::Byte> destination) noexcept = 0;

        /// @brief Current stream position if known.
        virtual NGIN::Utilities::Expected<UIntSize, IOError> Tell() const noexcept = 0;
    };
}// namespace NGIN::IO

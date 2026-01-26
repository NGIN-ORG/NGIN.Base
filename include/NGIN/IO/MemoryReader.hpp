#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/IO/IByteReader.hpp>
#include <NGIN/IO/IOError.hpp>

#include <cstring>

namespace NGIN::IO
{
    /// @brief In-memory implementation of IByteReader.
    class NGIN_BASE_API MemoryReader final : public IByteReader
    {
    public:
        explicit MemoryReader(std::span<const NGIN::Byte> data) noexcept
            : m_data(data.data()), m_size(data.size())
        {
        }

        NGIN::Utilities::Expected<UIntSize, IOError> Read(std::span<NGIN::Byte> destination) noexcept override
        {
            const UIntSize remaining = Remaining();
            const UIntSize toRead    = (destination.size() < remaining) ? destination.size() : remaining;
            if (toRead == 0)
                return NGIN::Utilities::Expected<UIntSize, IOError>(UIntSize {0});
            std::memcpy(destination.data(), m_data + m_offset, toRead);
            m_offset += toRead;
            return NGIN::Utilities::Expected<UIntSize, IOError>(toRead);
        }

        NGIN::Utilities::Expected<UIntSize, IOError> Skip(UIntSize bytes) noexcept override
        {
            const UIntSize remaining = Remaining();
            const UIntSize toSkip    = (bytes < remaining) ? bytes : remaining;
            m_offset += toSkip;
            return NGIN::Utilities::Expected<UIntSize, IOError>(toSkip);
        }

        NGIN::Utilities::Expected<UIntSize, IOError> Peek(std::span<NGIN::Byte> destination) noexcept override
        {
            const UIntSize remaining = Remaining();
            const UIntSize toRead    = (destination.size() < remaining) ? destination.size() : remaining;
            if (toRead == 0)
                return NGIN::Utilities::Expected<UIntSize, IOError>(UIntSize {0});
            std::memcpy(destination.data(), m_data + m_offset, toRead);
            return NGIN::Utilities::Expected<UIntSize, IOError>(toRead);
        }

        NGIN::Utilities::Expected<UIntSize, IOError> Tell() const noexcept override
        {
            return NGIN::Utilities::Expected<UIntSize, IOError>(m_offset);
        }

        [[nodiscard]] UIntSize Remaining() const noexcept
        {
            return (m_offset < m_size) ? (m_size - m_offset) : 0;
        }

    private:
        const NGIN::Byte* m_data {nullptr};
        UIntSize          m_size {0};
        UIntSize          m_offset {0};
    };
}// namespace NGIN::IO

#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/Core/SourceSpan.hpp>

#include <span>
#include <string_view>

namespace NGIN::Serialization
{
    /// @brief Lightweight cursor over a contiguous byte buffer.
    class InputCursor
    {
    public:
        explicit InputCursor(std::span<const NGIN::Byte> data, bool trackLocation = false) noexcept
            : m_data(reinterpret_cast<const char*>(data.data())), m_size(data.size()), m_trackLocation(trackLocation)
        {
            if (m_trackLocation)
            {
                m_line   = 1;
                m_column = 1;
            }
        }

        explicit InputCursor(std::string_view data, bool trackLocation = false) noexcept
            : m_data(data.data()), m_size(data.size()), m_trackLocation(trackLocation)
        {
            if (m_trackLocation)
            {
                m_line   = 1;
                m_column = 1;
            }
        }

        [[nodiscard]] bool IsEof() const noexcept { return m_offset >= m_size; }

        [[nodiscard]] char Peek() const noexcept
        {
            if (IsEof())
                return '\0';
            return m_data[m_offset];
        }

        [[nodiscard]] char Peek(UIntSize offset) const noexcept
        {
            if (m_offset > m_size || offset >= m_size - m_offset)
                return '\0';
            return m_data[m_offset + offset];
        }

        void Advance(UIntSize count = 1) noexcept
        {
            const UIntSize available = m_offset <= m_size ? m_size - m_offset : 0;
            const UIntSize advance   = count < available ? count : available;
            for (UIntSize index = 0; index < advance; ++index)
            {
                const char c = m_data[m_offset];
                ++m_offset;
                if (!m_trackLocation)
                    continue;

                if (c == '\r')
                {
                    ++m_line;
                    m_column = 1;
                    m_previousWasCarriageReturn = true;
                }
                else if (c == '\n')
                {
                    if (!m_previousWasCarriageReturn)
                        ++m_line;
                    m_column = 1;
                    m_previousWasCarriageReturn = false;
                }
                else
                {
                    ++m_column;
                    m_previousWasCarriageReturn = false;
                }
            }
        }

        void SkipWhitespace() noexcept
        {
            while (true)
            {
                const char c = Peek();
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    Advance();
                    continue;
                }
                return;
            }
        }

        [[nodiscard]] UIntSize Offset() const noexcept { return m_offset; }

        [[nodiscard]] ParseLocation Location() const noexcept
        {
            return ParseLocation {m_offset, m_line, m_column};
        }

        [[nodiscard]] const char* CurrentPtr() const noexcept
        {
            return m_data ? m_data + m_offset : nullptr;
        }

        [[nodiscard]] const char* EndPtr() const noexcept
        {
            return m_data ? m_data + m_size : nullptr;
        }

        [[nodiscard]] std::string_view Remaining() const noexcept
        {
            if (!m_data || m_offset >= m_size)
                return {};
            return {m_data + m_offset, m_size - m_offset};
        }

        [[nodiscard]] SourceSpan SpanFrom(UIntSize begin, SourceId source = {}) const noexcept
        {
            return SourceSpan {
                    .source = source,
                    .begin  = begin,
                    .end    = m_offset,
            };
        }

    private:
        const char* m_data {nullptr};
        UIntSize    m_size {0};
        bool        m_trackLocation {false};
        bool        m_previousWasCarriageReturn {false};
        UIntSize    m_offset {0};
        UIntSize    m_line {0};
        UIntSize    m_column {0};
    };
}// namespace NGIN::Serialization

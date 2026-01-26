#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Core/ParseError.hpp>

#include <span>
#include <string_view>

namespace NGIN::Serialization
{
    /// @brief Lightweight cursor over a contiguous byte buffer.
    class InputCursor
    {
    public:
        explicit InputCursor(std::span<const NGIN::Byte> data, bool trackLocation = false) noexcept
            : m_current(reinterpret_cast<const char*>(data.data())), m_end(reinterpret_cast<const char*>(data.data()) + data.size()), m_trackLocation(trackLocation)
        {
            if (m_trackLocation)
            {
                m_line   = 1;
                m_column = 1;
            }
        }

        explicit InputCursor(std::string_view data, bool trackLocation = false) noexcept
            : m_current(data.data()), m_end(data.data() + data.size()), m_trackLocation(trackLocation)
        {
            if (m_trackLocation)
            {
                m_line   = 1;
                m_column = 1;
            }
        }

        [[nodiscard]] bool IsEof() const noexcept { return m_current >= m_end; }

        [[nodiscard]] char Peek() const noexcept
        {
            if (IsEof())
                return '\0';
            return *m_current;
        }

        [[nodiscard]] char Peek(UIntSize offset) const noexcept
        {
            const char* ptr = m_current + offset;
            if (ptr >= m_end)
                return '\0';
            return *ptr;
        }

        void Advance(UIntSize count = 1) noexcept
        {
            while (count-- > 0 && m_current < m_end)
            {
                const char c = *m_current++;
                ++m_offset;
                if (!m_trackLocation)
                    continue;

                if (c == '\r')
                {
                    if (m_current < m_end && *m_current == '\n')
                    {
                        ++m_current;
                        ++m_offset;
                    }
                    ++m_line;
                    m_column = 1;
                }
                else if (c == '\n')
                {
                    ++m_line;
                    m_column = 1;
                }
                else
                {
                    ++m_column;
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

        [[nodiscard]] const char* CurrentPtr() const noexcept { return m_current; }
        [[nodiscard]] const char* EndPtr() const noexcept { return m_end; }

    private:
        const char* m_current {nullptr};
        const char* m_end {nullptr};
        bool        m_trackLocation {false};
        UIntSize    m_offset {0};
        UIntSize    m_line {0};
        UIntSize    m_column {0};
    };
}// namespace NGIN::Serialization

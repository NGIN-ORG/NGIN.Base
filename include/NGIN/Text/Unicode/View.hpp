/// @file View.hpp
/// @brief UTF-8 code-point iteration over byte-oriented string views.
#pragma once

#include <NGIN/Text/Unicode/Convert.hpp>

#include <iterator>
#include <string_view>

namespace NGIN::Text::Unicode
{
    class Utf8View
    {
    public:
        class Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = CodePoint;
            using difference_type   = std::ptrdiff_t;

            Iterator() = default;
            Iterator(const Utf8View* view, UIntSize offset) noexcept
                : m_view(view), m_offset(offset)
            {
            }

            [[nodiscard]] CodePoint operator*() const noexcept
            {
                const DecodeResult decoded = DecodeUtf8(m_view->m_input, m_offset);
                if (decoded.error == EncodingError::None)
                    return decoded.codePoint;
                return detail::ReplacementCharacter;
            }

            Iterator& operator++() noexcept
            {
                if (m_offset >= m_view->m_endOffset)
                    return *this;

                const DecodeResult decoded = DecodeUtf8(m_view->m_input, m_offset);
                if (decoded.error == EncodingError::None)
                {
                    m_offset += decoded.unitsConsumed;
                }
                else if (m_view->m_policy == ErrorPolicy::Strict)
                {
                    m_offset = m_view->m_endOffset;
                }
                else
                {
                    m_offset += detail::ClampConsumed(decoded.unitsConsumed, m_view->m_endOffset - m_offset);
                }

                m_offset = m_view->NormalizeOffset(m_offset);
                return *this;
            }

            Iterator operator++(int) noexcept
            {
                Iterator copy = *this;
                ++(*this);
                return copy;
            }

            [[nodiscard]] bool operator==(const Iterator& other) const noexcept
            {
                return m_view == other.m_view && m_offset == other.m_offset;
            }

            [[nodiscard]] bool operator!=(const Iterator& other) const noexcept
            {
                return !(*this == other);
            }

        private:
            const Utf8View* m_view {nullptr};
            UIntSize        m_offset {0};
        };

        explicit Utf8View(std::string_view input, ErrorPolicy policy = ErrorPolicy::Strict) noexcept
            : m_input(input), m_policy(policy), m_endOffset(static_cast<UIntSize>(input.size()))
        {
            if (m_policy == ErrorPolicy::Strict)
            {
                UIntSize offset = 0;
                while (offset < m_input.size())
                {
                    const DecodeResult decoded = DecodeUtf8(m_input, offset);
                    if (decoded.error != EncodingError::None)
                    {
                        m_error     = ConversionError {decoded.error, offset};
                        m_hasError  = true;
                        m_endOffset = offset;
                        break;
                    }
                    offset += decoded.unitsConsumed;
                }
            }
        }

        [[nodiscard]] Iterator begin() const noexcept
        {
            return Iterator(this, NormalizeOffset(0));
        }

        [[nodiscard]] Iterator end() const noexcept
        {
            return Iterator(this, m_endOffset);
        }

        [[nodiscard]] bool HasError() const noexcept { return m_hasError; }
        [[nodiscard]] const ConversionError& GetError() const noexcept { return m_error; }
        [[nodiscard]] std::string_view       View() const noexcept { return m_input; }
        [[nodiscard]] ErrorPolicy            GetPolicy() const noexcept { return m_policy; }

    private:
        [[nodiscard]] UIntSize NormalizeOffset(UIntSize offset) const noexcept
        {
            if (m_policy != ErrorPolicy::Skip)
                return offset;

            while (offset < m_endOffset)
            {
                const DecodeResult decoded = DecodeUtf8(m_input, offset);
                if (decoded.error == EncodingError::None)
                    return offset;

                offset += detail::ClampConsumed(decoded.unitsConsumed, m_endOffset - offset);
            }
            return m_endOffset;
        }

        std::string_view m_input {};
        ErrorPolicy      m_policy {ErrorPolicy::Strict};
        UIntSize         m_endOffset {0};
        ConversionError  m_error {};
        bool             m_hasError {false};
    };
}// namespace NGIN::Text::Unicode

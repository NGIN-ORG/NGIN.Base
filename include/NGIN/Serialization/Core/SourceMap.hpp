#pragma once

#include <NGIN/Serialization/Core/SourceSpan.hpp>

#include <algorithm>
#include <string_view>

namespace NGIN::Serialization
{
    /// @brief Lazily maps byte offsets to one-based line and column positions.
    class SourceMap
    {
    public:
        constexpr explicit SourceMap(std::string_view source, SourceId sourceId = {}) noexcept
            : m_source(source), m_sourceId(sourceId)
        {
        }

        [[nodiscard]] SourceLocation Locate(UIntSize offset) const noexcept
        {
            SourceLocation result {
                    .source = m_sourceId,
                    .offset = (std::min)(offset, m_source.size()),
                    .line   = 1,
                    .column = 1,
            };

            bool previousWasCarriageReturn = false;
            for (UIntSize index = 0; index < result.offset; ++index)
            {
                const char value = m_source[index];
                if (value == '\r')
                {
                    ++result.line;
                    result.column = 1;
                    previousWasCarriageReturn = true;
                }
                else if (value == '\n')
                {
                    if (!previousWasCarriageReturn)
                        ++result.line;
                    result.column = 1;
                    previousWasCarriageReturn = false;
                }
                else
                {
                    ++result.column;
                    previousWasCarriageReturn = false;
                }
            }
            return result;
        }

    private:
        std::string_view m_source {};
        SourceId         m_sourceId {};
    };
}// namespace NGIN::Serialization

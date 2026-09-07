#pragma once

#include "ParseStorage.hpp"
#include <NGIN/Serialization/Core/ParseDiagnostic.hpp>
#include <array>
#include <string>
#include <string_view>

namespace NGIN::Serialization::detail
{
    using BudgetString = std::basic_string<char, std::char_traits<char>, BudgetAllocator<char>>;

    // A token spanning chunks is copied once into retained storage. Complete tokens
    // are read directly from the caller's chunk. Scanning resumes where it stopped.
    class IncrementalInput
    {
    public:
        enum class Format
        {
            Json,
            Xml
        };
        IncrementalInput(Format format, AllocationBudget& budget) : m_format(format), m_pending(BudgetAllocator<char> {budget}) {}
        [[nodiscard]] UIntSize BufferedBytes() const noexcept { return m_pending.size(); }
        [[nodiscard]] UIntSize Offset() const noexcept { return m_offset; }
        [[nodiscard]] bool     Empty() const noexcept { return m_mode == Mode::Start; }
        void                   Reset() noexcept
        {
            m_pending.clear();
            m_mode   = Mode::Start;
            m_offset = 0;
            m_line   = 1;
            m_column = 1;
            m_cr     = false;
        }
        void Translate(ParseDiagnostic& error, bool related = true) const
        {
            if (error.location.line == 1)
                error.location.column += m_column - 1;
            error.location.line += m_line - 1;
            error.location.offset += m_offset;
            error.span.begin += m_offset;
            error.span.end += m_offset;
            if (related && error.related)
            {
                error.related->begin += m_offset;
                error.related->end += m_offset;
            }
        }
        template<class Consume>
        bool Feed(std::string_view chunk, Consume consume)
        {
            while (!chunk.empty())
            {
                UIntSize count    = 0;
                bool     complete = false;
                while (count < chunk.size())
                {
                    bool take = true;
                    complete  = Scan(chunk[count], take);
                    if (take)
                        ++count;
                    if (complete)
                        break;
                }
                const auto part = chunk.substr(0, count);
                if (!m_pending.empty())
                    m_pending.append(part.data(), part.size());
                if (complete)
                {
                    const std::string_view token = m_pending.empty() ? part : std::string_view {m_pending.data(), m_pending.size()};
                    if (!consume(token))
                        return false;
                    Advance(token);
                    m_pending.clear();
                    m_mode = Mode::Start;
                }
                else if (m_pending.empty())
                    m_pending.assign(part.data(), part.size());
                chunk.remove_prefix(count);
            }
            return true;
        }
        template<class Consume>
        bool Finish(Consume consume)
        {
            if (m_mode == Mode::Start)
                return true;
            const std::string_view token {m_pending.data(), m_pending.size()};
            if (!consume(token))
                return false;
            Advance(token);
            m_pending.clear();
            m_mode = Mode::Start;
            return true;
        }

    private:
        enum class Mode
        {
            Start,
            String,
            Word,
            Slash,
            LineComment,
            BlockComment,
            XmlText,
            XmlPrefix,
            Tag,
            Comment,
            CData,
            PI
        };
        static bool Space(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
        static bool Punctuation(char c) noexcept { return c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ','; }
        bool        Scan(char c, bool& take) noexcept
        {
            if (m_mode == Mode::Start)
            {
                m_escape         = false;
                m_quote          = 0;
                m_brackets       = 0;
                m_previous       = 0;
                m_beforePrevious = 0;
                m_prefixSize     = 0;
                if (m_format == Format::Json)
                {
                    if (Space(c) || Punctuation(c))
                        return true;
                    m_mode = c == '"' ? Mode::String : c == '/' ? Mode::Slash
                                                                : Mode::Word;
                }
                else if (c == '<')
                {
                    m_mode                   = Mode::XmlPrefix;
                    m_prefix[m_prefixSize++] = c;
                }
                else
                    m_mode = Mode::XmlText;
                return false;
            }
            switch (m_mode)
            {
                case Mode::String:
                    if (m_escape)
                        m_escape = false;
                    else if (c == '\\')
                        m_escape = true;
                    else if (c == '"')
                        return true;
                    break;
                case Mode::Word:
                    if (Space(c) || Punctuation(c) || c == '/' || c == '"')
                    {
                        take = false;
                        return true;
                    }
                    break;
                case Mode::Slash:
                    if (c == '/')
                        m_mode = Mode::LineComment;
                    else if (c == '*')
                    {
                        m_mode     = Mode::BlockComment;
                        m_previous = 0;
                        return false;
                    }
                    else
                        return true;
                    break;
                case Mode::LineComment:
                    if (c == '\n' || c == '\r')
                        return true;
                    break;
                case Mode::BlockComment:
                    if (m_previous == '*' && c == '/')
                        return true;
                    break;
                case Mode::XmlText:
                    if (c == '<')
                    {
                        take = false;
                        return true;
                    }
                    break;
                case Mode::XmlPrefix: {
                    m_prefix[m_prefixSize++] = c;
                    const std::string_view prefix {m_prefix.data(), m_prefixSize};
                    if (prefix == "<?")
                    {
                        m_mode     = Mode::PI;
                        m_previous = m_beforePrevious = 0;
                        return false;
                    }
                    else if (prefix == "<!--")
                    {
                        m_mode     = Mode::Comment;
                        m_previous = m_beforePrevious = 0;
                        return false;
                    }
                    else if (prefix == "<![CDATA[")
                    {
                        m_mode     = Mode::CData;
                        m_previous = m_beforePrevious = 0;
                        return false;
                    }
                    else if (!std::string_view {"<!--"}.starts_with(prefix) && !std::string_view {"<![CDATA["}.starts_with(prefix))
                    {
                        m_mode = Mode::Tag;
                        if (c == '>')
                            return true;
                        if (c == '\'' || c == '"')
                            m_quote = c;
                        if (c == '[')
                            ++m_brackets;
                    }
                    break;
                }
                case Mode::Tag:
                    if (m_quote)
                    {
                        if (c == m_quote)
                            m_quote = 0;
                    }
                    else if (c == '\'' || c == '"')
                        m_quote = c;
                    else if (c == '[')
                        ++m_brackets;
                    else if (c == ']' && m_brackets)
                        --m_brackets;
                    else if (c == '>' && !m_brackets)
                        return true;
                    break;
                case Mode::Comment:
                    if (m_beforePrevious == '-' && m_previous == '-' && c == '>')
                        return true;
                    break;
                case Mode::CData:
                    if (m_beforePrevious == ']' && m_previous == ']' && c == '>')
                        return true;
                    break;
                case Mode::PI:
                    if (m_previous == '?' && c == '>')
                        return true;
                    break;
                case Mode::Start:
                    break;
            }
            m_beforePrevious = m_previous;
            m_previous       = c;
            return false;
        }
        void Advance(std::string_view token) noexcept
        {
            m_offset += token.size();
            for (char c: token)
            {
                if (c == '\r')
                {
                    ++m_line;
                    m_column = 1;
                }
                else if (c == '\n')
                {
                    if (!m_cr)
                        ++m_line;
                    m_column = 1;
                }
                else
                    ++m_column;
                m_cr = c == '\r';
            }
        }
        Format              m_format;
        BudgetString        m_pending;
        Mode                m_mode {Mode::Start};
        UIntSize            m_offset {0}, m_line {1}, m_column {1};
        UIntSize            m_brackets {0}, m_prefixSize {0};
        std::array<char, 9> m_prefix {};
        char                m_quote {0}, m_previous {0}, m_beforePrevious {0};
        bool                m_escape {false}, m_cr {false};
    };
}// namespace NGIN::Serialization::detail

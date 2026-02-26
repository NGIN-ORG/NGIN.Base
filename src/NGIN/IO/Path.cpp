#include <NGIN/IO/Path.hpp>

#include <NGIN/Containers/Vector.hpp>

#include <cctype>
#include <string>

namespace NGIN::IO
{
    namespace
    {
        [[nodiscard]] inline bool IsSeparator(char c) noexcept
        {
            return c == Path::Separator() || c == Path::AltSeparator();
        }

        [[nodiscard]] inline bool IsDrivePrefix(std::string_view path) noexcept
        {
            return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
        }

        [[nodiscard]] inline std::size_t FindFilenameStart(std::string_view view, std::size_t endExclusive) noexcept
        {
            if (endExclusive == 0)
                return std::string_view::npos;
            const std::size_t pos = view.find_last_of("/\\", endExclusive - 1);
            return (pos == std::string_view::npos) ? 0 : (pos + 1);
        }

        [[nodiscard]] NGIN::Containers::Vector<std::string_view> SplitNormalizedComponents(std::string_view value)
        {
            NGIN::Containers::Vector<std::string_view> out;
            std::size_t pos = 0;
            if (IsDrivePrefix(value))
            {
                pos = 2;
                if (pos < value.size() && IsSeparator(value[pos]))
                    ++pos;
            }
            else if (!value.empty() && IsSeparator(value.front()))
            {
                pos = 1;
            }

            while (pos < value.size())
            {
                while (pos < value.size() && IsSeparator(value[pos]))
                    ++pos;
                if (pos >= value.size())
                    break;
                const std::size_t start = pos;
                while (pos < value.size() && !IsSeparator(value[pos]))
                    ++pos;
                out.PushBack(value.substr(start, pos - start));
            }
            return out;
        }
    }// namespace

    Path::Path(std::string_view path)
        : m_path(path)
    {
    }

    Path::Path(const char* path)
        : m_path(path ? path : "")
    {
    }

    bool Path::IsEmpty() const noexcept
    {
        return m_path.Size() == 0;
    }

    bool Path::IsAbsolute() const noexcept
    {
        const std::string_view view = m_path;
        if (view.empty())
            return false;
        if (IsDrivePrefix(view))
        {
            return view.size() >= 3 && IsSeparator(view[2]);
        }
        return IsSeparator(view.front());
    }

    bool Path::IsRelative() const noexcept
    {
        return !IsAbsolute();
    }

    std::string_view Path::View() const noexcept
    {
        return std::string_view {m_path.Data(), m_path.Size()};
    }

    const NGIN::Text::String& Path::String() const noexcept
    {
        return m_path;
    }

    std::string_view Path::Filename() const noexcept
    {
        const std::string_view view = m_path;
        if (view.empty())
            return {};
        std::size_t end = view.size();
        while (end > 0 && IsSeparator(view[end - 1]))
            --end;
        if (end == 0)
            return {};
        const std::size_t pos = view.find_last_of("/\\", end - 1);
        if (pos == std::string_view::npos)
            return view.substr(0, end);
        return view.substr(pos + 1, end - pos - 1);
    }

    bool Path::HasFilename() const noexcept
    {
        return !Filename().empty();
    }

    std::string_view Path::Stem() const noexcept
    {
        const std::string_view name = Filename();
        if (name.empty())
            return {};
        const std::size_t dot = name.find_last_of('.');
        if (dot == std::string_view::npos || dot == 0)
            return name;
        return name.substr(0, dot);
    }

    std::string_view Path::Extension() const noexcept
    {
        const std::string_view name = Filename();
        if (name.empty())
            return {};
        const std::size_t dot = name.find_last_of('.');
        if (dot == std::string_view::npos || dot == 0)
            return {};
        return name.substr(dot + 1);
    }

    bool Path::HasExtension() const noexcept
    {
        return !Extension().empty();
    }

    Path Path::Parent() const
    {
        const std::string_view view = m_path;
        if (view.empty())
            return Path {};

        std::size_t end = view.size();
        while (end > 0 && IsSeparator(view[end - 1]))
            --end;
        if (end == 0)
            return Path {std::string_view {view.data(), 1}};

        const std::size_t start = FindFilenameStart(view, end);
        if (start == std::string_view::npos)
            return Path {};
        if (start == 0)
        {
            if (IsDrivePrefix(view))
            {
                if (view.size() >= 3 && IsSeparator(view[2]))
                    return Path {view.substr(0, 3)};
                return Path {view.substr(0, 2)};
            }
            if (!view.empty() && IsSeparator(view.front()))
                return Path {"/"};
            return Path {};
        }

        std::size_t parentEnd = start;
        while (parentEnd > 0 && IsSeparator(view[parentEnd - 1]))
            --parentEnd;
        return Path {view.substr(0, parentEnd)};
    }

    bool Path::IsRoot() const noexcept
    {
        const std::string_view view = View();
        if (view == "/")
            return true;
        if (view.size() == 3 && IsDrivePrefix(view) && IsSeparator(view[2]))
            return true;
        return false;
    }

    void Path::Normalize()
    {
        const std::string_view view = m_path;
        if (view.empty())
            return;

        std::string_view prefix {};
        std::size_t      pos      = 0;
        bool             absolute = false;

        if (IsDrivePrefix(view))
        {
            prefix   = view.substr(0, 2);
            pos      = 2;
            absolute = view.size() > 2 && IsSeparator(view[2]);
            while (pos < view.size() && IsSeparator(view[pos]))
                ++pos;
        }
        else if (IsSeparator(view.front()))
        {
            absolute = true;
            while (pos < view.size() && IsSeparator(view[pos]))
                ++pos;
        }

        NGIN::Containers::Vector<std::string_view> segments;

        while (pos < view.size())
        {
            const std::size_t start = pos;
            while (pos < view.size() && !IsSeparator(view[pos]))
                ++pos;
            std::string_view segment = view.substr(start, pos - start);
            while (pos < view.size() && IsSeparator(view[pos]))
                ++pos;

            if (segment.empty() || segment == ".")
                continue;
            if (segment == "..")
            {
                if (segments.Size() > 0 && segments[segments.Size() - 1] != "..")
                {
                    segments.PopBack();
                    continue;
                }
                if (!absolute)
                {
                    segments.PushBack(segment);
                }
                continue;
            }
            segments.PushBack(segment);
        }

        NGIN::Text::String normalized;
        if (!prefix.empty())
        {
            normalized.Append(prefix);
            if (absolute)
                normalized.Append("/");
        }
        else if (absolute)
        {
            normalized.Append("/");
        }

        for (std::size_t i = 0; i < segments.Size(); ++i)
        {
            if (i > 0 || (!prefix.empty() && absolute) || (prefix.empty() && absolute))
            {
                if (normalized.Size() > 0 && normalized[normalized.Size() - 1] != '/')
                    normalized.Append("/");
            }
            normalized.Append(segments[i]);
        }

        if (normalized.Size() == 0)
        {
            if (!prefix.empty())
                normalized.Append(prefix);
        }

        m_path = std::move(normalized);
    }

    Path Path::LexicallyNormal() const
    {
        Path copy = *this;
        copy.Normalize();
        return copy;
    }

    Path Path::LexicallyRelativeTo(const Path& base) const
    {
        Path lhs = LexicallyNormal();
        Path rhs = base.LexicallyNormal();
        const std::string_view lhsView = lhs.View();
        const std::string_view rhsView = rhs.View();

        if (lhs.IsAbsolute() != rhs.IsAbsolute())
            return lhs;

        const bool lhsDrive = IsDrivePrefix(lhsView);
        const bool rhsDrive = IsDrivePrefix(rhsView);
        if (lhsDrive != rhsDrive)
            return lhs;
        if (lhsDrive && rhsDrive)
        {
            const char l0 = static_cast<char>(std::tolower(static_cast<unsigned char>(lhsView[0])));
            const char r0 = static_cast<char>(std::tolower(static_cast<unsigned char>(rhsView[0])));
            if (l0 != r0)
                return lhs;
        }

        auto lhsParts = SplitNormalizedComponents(lhsView);
        auto rhsParts = SplitNormalizedComponents(rhsView);

        std::size_t common = 0;
        while (common < lhsParts.Size() && common < rhsParts.Size() && lhsParts[common] == rhsParts[common])
            ++common;

        NGIN::Text::String out;
        bool wrote = false;
        for (std::size_t i = common; i < rhsParts.Size(); ++i)
        {
            if (wrote)
                out.Append("/");
            out.Append("..");
            wrote = true;
        }
        for (std::size_t i = common; i < lhsParts.Size(); ++i)
        {
            if (wrote)
                out.Append("/");
            out.Append(lhsParts[i]);
            wrote = true;
        }

        if (!wrote)
            out.Append(".");

        return Path {std::string_view {out.Data(), out.Size()}};
    }

    bool Path::StartsWith(const Path& prefix) const noexcept
    {
        const std::string_view value = View();
        const std::string_view pref  = prefix.View();
        if (pref.empty())
            return true;
        if (pref.size() > value.size())
            return false;
        if (value.substr(0, pref.size()) != pref)
            return false;
        if (pref.size() == value.size())
            return true;
        return IsSeparator(value[pref.size()]) || IsSeparator(pref.back());
    }

    bool Path::EndsWith(const Path& suffix) const noexcept
    {
        const std::string_view value = View();
        const std::string_view suf   = suffix.View();
        if (suf.empty())
            return true;
        if (suf.size() > value.size())
            return false;
        return value.substr(value.size() - suf.size()) == suf;
    }

    Path Path::Join(std::string_view segment) const
    {
        Path copy = *this;
        copy.Append(segment);
        return copy;
    }

    Path& Path::Append(std::string_view segment)
    {
        if (segment.empty())
            return *this;

        if (m_path.Size() > 0 && !IsSeparator(m_path[m_path.Size() - 1]))
            m_path.Append("/");
        m_path.Append(segment);
        return *this;
    }

    Path& Path::ReplaceExtension(std::string_view extension)
    {
        const std::string_view view = View();
        const std::string_view name = Filename();
        if (name.empty())
            return *this;

        const std::size_t suffixLen = name.size();
        const std::size_t start = view.size() - suffixLen;
        const std::size_t dot = name.find_last_of('.');
        NGIN::Text::String out;
        if (dot == std::string_view::npos || dot == 0)
        {
            out.Append(view);
        }
        else
        {
            out.Append(view.substr(0, start + dot));
        }

        if (!extension.empty())
        {
            if (extension.front() != '.')
                out.Append(".");
            out.Append(extension);
        }
        m_path = std::move(out);
        return *this;
    }

    Path& Path::RemoveFilename()
    {
        *this = Parent();
        return *this;
    }

    Path Path::FromNative(std::string_view path)
    {
        Path p {path};
        return p;
    }

    NGIN::Text::String Path::ToNative() const
    {
        NGIN::Text::String value {View()};
#if defined(_WIN32)
        for (std::size_t i = 0; i < value.Size(); ++i)
        {
            if (value[i] == '/')
                value[i] = '\\';
        }
#endif
        return value;
    }

}// namespace NGIN::IO

#include <NGIN/IO/Path.hpp>

#include <NGIN/Containers/Vector.hpp>

#include <cctype>

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

}// namespace NGIN::IO

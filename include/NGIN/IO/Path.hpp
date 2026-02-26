#pragma once

#include <NGIN/Text/String.hpp>
#include <NGIN/Defines.hpp>

#include <string_view>

namespace NGIN::IO
{
    /// @brief Lightweight path helper with normalization and join utilities.
    class NGIN_BASE_API Path
    {
    public:
        Path() = default;
        explicit Path(std::string_view path);
        explicit Path(const char* path);

        [[nodiscard]] bool IsEmpty() const noexcept;
        [[nodiscard]] bool IsAbsolute() const noexcept;
        [[nodiscard]] bool IsRelative() const noexcept;

        [[nodiscard]] std::string_view          View() const noexcept;
        [[nodiscard]] const NGIN::Text::String& String() const noexcept;

        [[nodiscard]] std::string_view Filename() const noexcept;
        [[nodiscard]] bool             HasFilename() const noexcept;
        [[nodiscard]] std::string_view Stem() const noexcept;
        [[nodiscard]] std::string_view Extension() const noexcept;
        [[nodiscard]] bool             HasExtension() const noexcept;
        [[nodiscard]] Path             Parent() const;
        [[nodiscard]] bool             IsRoot() const noexcept;

        void Normalize();
        [[nodiscard]] Path LexicallyNormal() const;
        [[nodiscard]] Path LexicallyRelativeTo(const Path& base) const;
        [[nodiscard]] bool StartsWith(const Path& prefix) const noexcept;
        [[nodiscard]] bool EndsWith(const Path& suffix) const noexcept;

        [[nodiscard]] Path Join(std::string_view segment) const;
        Path&              Append(std::string_view segment);
        Path&              ReplaceExtension(std::string_view extension);
        Path&              RemoveFilename();

        [[nodiscard]] static Path FromNative(std::string_view path);
        [[nodiscard]] NGIN::Text::String ToNative() const;

        static constexpr char Separator() noexcept { return '/'; }
        static constexpr char AltSeparator() noexcept { return '\\'; }

    private:
        NGIN::Text::String m_path {};
    };
}// namespace NGIN::IO

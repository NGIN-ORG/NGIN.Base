#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Text/String.hpp>

#include <string_view>

namespace NGIN::IO
{
    /// @brief Lightweight path helper with normalization and join utilities.
    class NGIN_IO_API Path
    {
    public:
        /// @brief Constructs an empty path.
        Path() = default;
        /// @brief Constructs and normalizes a path from UTF-8 text.
        explicit Path(std::string_view path);
        /// @brief Constructs and normalizes a path from a null-terminated UTF-8 string.
        explicit Path(const char* path);

        /// @brief Returns whether the path contains no characters.
        [[nodiscard]] bool IsEmpty() const noexcept;
        /// @brief Returns whether the path is absolute on the current platform.
        [[nodiscard]] bool IsAbsolute() const noexcept;
        /// @brief Returns whether the path is non-empty and not absolute.
        [[nodiscard]] bool IsRelative() const noexcept;

        /// @brief Returns a non-owning UTF-8 view valid until this path is modified.
        [[nodiscard]] std::string_view View() const noexcept;
        /// @brief Returns the owned normalized UTF-8 string.
        [[nodiscard]] const NGIN::Text::String& String() const noexcept;

        /// @brief Returns the final path component.
        [[nodiscard]] std::string_view Filename() const noexcept;
        /// @brief Returns whether the path has a final filename component.
        [[nodiscard]] bool HasFilename() const noexcept;
        /// @brief Returns the filename without its final extension.
        [[nodiscard]] std::string_view Stem() const noexcept;
        /// @brief Returns the final filename extension, including the leading dot.
        [[nodiscard]] std::string_view Extension() const noexcept;
        /// @brief Returns whether the filename has an extension.
        [[nodiscard]] bool HasExtension() const noexcept;
        /// @brief Returns the path excluding its final filename component.
        [[nodiscard]] Path Parent() const;
        /// @brief Returns whether this path identifies a filesystem root.
        [[nodiscard]] bool IsRoot() const noexcept;

        /// @brief Normalizes separators and lexical `.` and `..` components in place.
        void Normalize();
        /// @brief Returns a lexically normalized copy of this path.
        [[nodiscard]] Path LexicallyNormal() const;
        /// @brief Returns the lexical path from @p base to this path, or an empty path when incompatible.
        [[nodiscard]] Path LexicallyRelativeTo(const Path& base) const;
        /// @brief Returns whether this path begins with the complete prefix path.
        [[nodiscard]] bool StartsWith(const Path& prefix) const noexcept;
        /// @brief Returns whether this path ends with the complete suffix path.
        [[nodiscard]] bool EndsWith(const Path& suffix) const noexcept;

        /// @brief Returns a normalized path with one segment appended.
        [[nodiscard]] Path Join(std::string_view segment) const;
        /// @brief Appends a segment and normalizes the resulting path.
        Path& Append(std::string_view segment);
        /// @brief Replaces the final extension; a leading dot is optional.
        Path& ReplaceExtension(std::string_view extension);
        /// @brief Removes the final filename component in place.
        Path& RemoveFilename();

        /// @brief Converts a platform-native path representation to normalized UTF-8 form.
        [[nodiscard]] static Path FromNative(std::string_view path);
        /// @brief Converts this normalized path to the current platform's native separators.
        [[nodiscard]] NGIN::Text::String ToNative() const;

        /// @brief Returns the canonical separator used by normalized paths.
        static constexpr char Separator() noexcept { return '/'; }
        /// @brief Returns the accepted alternate path separator.
        static constexpr char AltSeparator() noexcept { return '\\'; }

    private:
        NGIN::Text::String m_path {};
    };
}// namespace NGIN::IO

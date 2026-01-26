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
        [[nodiscard]] std::string_view Extension() const noexcept;

        void Normalize();

        [[nodiscard]] Path Join(std::string_view segment) const;
        Path&              Append(std::string_view segment);

        static constexpr char Separator() noexcept { return '/'; }
        static constexpr char AltSeparator() noexcept { return '\\'; }

    private:
        NGIN::Text::String m_path {};
    };
}// namespace NGIN::IO

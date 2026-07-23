#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Core/SourceSpan.hpp>
#include <NGIN/Text/String.hpp>

#include <span>
#include <string_view>
#include <utility>

namespace NGIN::Serialization
{
    /// @brief Non-owning UTF-8 parser input.
    class BorrowedTextView
    {
    public:
        constexpr BorrowedTextView() noexcept = default;

        constexpr explicit BorrowedTextView(std::string_view text, SourceId source = {}) noexcept
            : m_text(text), m_source(source)
        {
        }

        [[nodiscard]] constexpr std::string_view View() const noexcept { return m_text; }
        [[nodiscard]] constexpr SourceId         Source() const noexcept { return m_source; }
        [[nodiscard]] constexpr bool             Empty() const noexcept { return m_text.empty(); }

    private:
        std::string_view m_text {};
        SourceId         m_source {};
    };

    /// @brief Owning UTF-8 parser input used by self-contained documents.
    class OwnedTextBuffer
    {
    public:
        OwnedTextBuffer() = default;

        explicit OwnedTextBuffer(const char* text, SourceId source = {})
            : m_text(std::string_view {text ? text : ""}), m_source(source)
        {
        }

        explicit OwnedTextBuffer(std::string_view text, SourceId source = {})
            : m_text(text), m_source(source)
        {
        }

        explicit OwnedTextBuffer(NGIN::Text::String text, SourceId source = {}) noexcept
            : m_text(std::move(text)), m_source(source)
        {
        }

        [[nodiscard]] std::string_view View() const noexcept { return m_text.View(); }
        [[nodiscard]] SourceId         Source() const noexcept { return m_source; }
        [[nodiscard]] bool             Empty() const noexcept { return m_text.Empty(); }
        [[nodiscard]] UIntSize         Size() const noexcept { return m_text.Size(); }

        [[nodiscard]] BorrowedTextView Borrow() const noexcept
        {
            return BorrowedTextView {View(), m_source};
        }

        [[nodiscard]] NGIN::Text::String&       Text() noexcept { return m_text; }
        [[nodiscard]] const NGIN::Text::String& Text() const noexcept { return m_text; }

    private:
        NGIN::Text::String m_text {};
        SourceId           m_source {};
    };

    /// @brief Owning mutable UTF-8 input for explicit in-situ parsing.
    class MutableTextBuffer
    {
    public:
        MutableTextBuffer() = default;

        explicit MutableTextBuffer(const char* text, SourceId source = {})
            : m_text(std::string_view {text ? text : ""}), m_source(source)
        {
        }

        explicit MutableTextBuffer(std::string_view text, SourceId source = {})
            : m_text(text), m_source(source)
        {
        }

        explicit MutableTextBuffer(NGIN::Text::String text, SourceId source = {}) noexcept
            : m_text(std::move(text)), m_source(source)
        {
        }

        [[nodiscard]] std::string_view View() const noexcept { return m_text.View(); }
        [[nodiscard]] SourceId         Source() const noexcept { return m_source; }
        [[nodiscard]] UIntSize         Size() const noexcept { return m_text.Size(); }

        [[nodiscard]] std::span<NGIN::Byte> MutableBytes() noexcept
        {
            return {
                    reinterpret_cast<NGIN::Byte*>(m_text.Data()),
                    m_text.Size(),
            };
        }

        [[nodiscard]] OwnedTextBuffer TakeOwned() && noexcept
        {
            return OwnedTextBuffer {std::move(m_text), m_source};
        }

        [[nodiscard]] NGIN::Text::String& Text() noexcept { return m_text; }

    private:
        NGIN::Text::String m_text {};
        SourceId           m_source {};
    };
}// namespace NGIN::Serialization

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
        /// @brief Constructs an empty borrowed input.
        constexpr BorrowedTextView() noexcept = default;

        /// @brief Borrows UTF-8 text and associates it with a caller-defined source identifier.
        /// @note The text storage must outlive this view and every parse operation using it.
        constexpr explicit BorrowedTextView(std::string_view text, SourceId source = {}) noexcept
            : m_text(text), m_source(source)
        {
        }

        /// @brief Returns the borrowed UTF-8 text.
        [[nodiscard]] constexpr std::string_view View() const noexcept { return m_text; }
        /// @brief Returns the registered source identifier.
        [[nodiscard]] constexpr SourceId Source() const noexcept { return m_source; }
        /// @brief Returns whether the borrowed text is empty.
        [[nodiscard]] constexpr bool Empty() const noexcept { return m_text.empty(); }

    private:
        std::string_view m_text {};
        SourceId         m_source {};
    };

    /// @brief Owning UTF-8 parser input used by self-contained documents.
    class OwnedTextBuffer
    {
    public:
        /// @brief Constructs an empty owned input.
        OwnedTextBuffer() = default;

        /// @brief Copies a null-terminated UTF-8 string into owned storage.
        explicit OwnedTextBuffer(const char* text, SourceId source = {})
            : m_text(std::string_view {text ? text : ""}), m_source(source)
        {
        }

        /// @brief Copies UTF-8 text into owned storage.
        explicit OwnedTextBuffer(std::string_view text, SourceId source = {})
            : m_text(text), m_source(source)
        {
        }

        /// @brief Takes ownership of a UTF-8 string.
        explicit OwnedTextBuffer(NGIN::Text::String text, SourceId source = {}) noexcept
            : m_text(std::move(text)), m_source(source)
        {
        }

        /// @brief Returns a borrowed view of the owned UTF-8 text.
        [[nodiscard]] std::string_view View() const noexcept { return m_text.View(); }
        /// @brief Returns the registered source identifier.
        [[nodiscard]] SourceId Source() const noexcept { return m_source; }
        /// @brief Returns whether the owned text is empty.
        [[nodiscard]] bool Empty() const noexcept { return m_text.Empty(); }
        /// @brief Returns the text size in bytes.
        [[nodiscard]] UIntSize Size() const noexcept { return m_text.Size(); }

        /// @brief Returns a non-owning parser input tied to this buffer's lifetime.
        [[nodiscard]] BorrowedTextView Borrow() const noexcept
        {
            return BorrowedTextView {View(), m_source};
        }

        /// @brief Returns mutable access to the owned string.
        /// @warning Mutation invalidates views previously returned by this object.
        [[nodiscard]] NGIN::Text::String& Text() noexcept { return m_text; }
        /// @brief Returns the owned string.
        [[nodiscard]] const NGIN::Text::String& Text() const noexcept { return m_text; }

    private:
        NGIN::Text::String m_text {};
        SourceId           m_source {};
    };

    /// @brief Owning mutable UTF-8 input for explicit in-situ parsing.
    class MutableTextBuffer
    {
    public:
        /// @brief Constructs an empty mutable input.
        MutableTextBuffer() = default;

        /// @brief Copies a null-terminated UTF-8 string into mutable storage.
        explicit MutableTextBuffer(const char* text, SourceId source = {})
            : m_text(std::string_view {text ? text : ""}), m_source(source)
        {
        }

        /// @brief Copies UTF-8 text into mutable storage.
        explicit MutableTextBuffer(std::string_view text, SourceId source = {})
            : m_text(text), m_source(source)
        {
        }

        /// @brief Takes ownership of a UTF-8 string for in-situ parsing.
        explicit MutableTextBuffer(NGIN::Text::String text, SourceId source = {}) noexcept
            : m_text(std::move(text)), m_source(source)
        {
        }

        /// @brief Returns a read-only view of the mutable UTF-8 text.
        [[nodiscard]] std::string_view View() const noexcept { return m_text.View(); }
        /// @brief Returns the registered source identifier.
        [[nodiscard]] SourceId Source() const noexcept { return m_source; }
        /// @brief Returns the text size in bytes.
        [[nodiscard]] UIntSize Size() const noexcept { return m_text.Size(); }

        /// @brief Returns mutable access to the text bytes for explicit in-situ parsing.
        /// @note The span is invalidated by string mutation or ownership transfer.
        [[nodiscard]] std::span<NGIN::Byte> MutableBytes() noexcept
        {
            return {
                    reinterpret_cast<NGIN::Byte*>(m_text.Data()),
                    m_text.Size(),
            };
        }

        /// @brief Transfers the text and source identifier into an immutable owning buffer.
        [[nodiscard]] OwnedTextBuffer TakeOwned() && noexcept
        {
            return OwnedTextBuffer {std::move(m_text), m_source};
        }

        /// @brief Returns mutable access to the underlying string.
        [[nodiscard]] NGIN::Text::String& Text() noexcept { return m_text; }

    private:
        NGIN::Text::String m_text {};
        SourceId           m_source {};
    };
}// namespace NGIN::Serialization

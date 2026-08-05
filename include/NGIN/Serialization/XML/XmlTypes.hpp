#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Core/SourceSpan.hpp>

#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace NGIN::Serialization::XML
{
    namespace detail
    {
        struct DocumentState;
        struct DocumentAccess;
        struct SyntaxState;
    }// namespace detail

    struct NodeId
    {
        UInt32 value {static_cast<UInt32>(-1)};

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != static_cast<UInt32>(-1);
        }

        [[nodiscard]] friend constexpr bool operator==(NodeId, NodeId) noexcept = default;
    };

    enum class NodeKind : UInt8
    {
        Element,
        Text,
        CData,
        Comment,
        ProcessingInstruction,
    };

    class ElementView;
    class AttributeView;

    class NGIN_SERIALIZATION_API NodeView
    {
    public:
        NodeView() noexcept = default;

        [[nodiscard]] bool                            IsValid() const noexcept;
        [[nodiscard]] NodeKind                        Kind() const noexcept;
        [[nodiscard]] SourceSpan                      Span() const noexcept;
        [[nodiscard]] std::string_view                Name() const noexcept;
        [[nodiscard]] std::optional<ElementView>      TryElement() const noexcept;
        [[nodiscard]] std::optional<std::string_view> TryText() const noexcept;

    private:
        friend class ElementView;
        friend class Document;
        friend class BorrowedDocument;
        friend class ChildRange;

        NodeView(const detail::DocumentState* state, NodeId id) noexcept;

        const detail::DocumentState* m_state {nullptr};
        NodeId                       m_id {};
    };

    class NGIN_SERIALIZATION_API AttributeView
    {
    public:
        AttributeView() noexcept = default;

        [[nodiscard]] bool             IsValid() const noexcept;
        [[nodiscard]] std::string_view Name() const noexcept;
        [[nodiscard]] std::string_view Value() const noexcept;
        [[nodiscard]] SourceSpan       Span() const noexcept;
        [[nodiscard]] SourceSpan       NameSpan() const noexcept;
        [[nodiscard]] SourceSpan       ValueSpan() const noexcept;

    private:
        friend class ElementView;
        friend class AttributeRange;

        AttributeView(const detail::DocumentState* state, UInt32 index) noexcept;

        const detail::DocumentState* m_state {nullptr};
        UInt32                       m_index {0};
    };

    class NGIN_SERIALIZATION_API AttributeRange
    {
    public:
        constexpr AttributeRange() noexcept = default;

        class Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = AttributeView;
            using difference_type   = std::ptrdiff_t;

            [[nodiscard]] AttributeView operator*() const noexcept;
            Iterator&                   operator++() noexcept;
            [[nodiscard]] friend bool   operator==(const Iterator&, const Iterator&) noexcept = default;

        private:
            friend class AttributeRange;
            constexpr Iterator(const detail::DocumentState* state, UInt32 index) noexcept
                : m_state(state), m_index(index)
            {
            }
            const detail::DocumentState* m_state {nullptr};
            UInt32                       m_index {0};
        };

        [[nodiscard]] UIntSize      Size() const noexcept { return m_count; }
        [[nodiscard]] bool          Empty() const noexcept { return m_count == 0; }
        [[nodiscard]] AttributeView operator[](UIntSize index) const noexcept;
        [[nodiscard]] Iterator      begin() const noexcept { return Iterator {m_state, m_begin}; }
        [[nodiscard]] Iterator      end() const noexcept { return Iterator {m_state, m_begin + m_count}; }

    private:
        friend class ElementView;
        constexpr AttributeRange(const detail::DocumentState* state, UInt32 begin, UInt32 count) noexcept
            : m_state(state), m_begin(begin), m_count(count)
        {
        }
        const detail::DocumentState* m_state {nullptr};
        UInt32                       m_begin {0};
        UInt32                       m_count {0};
    };

    class NGIN_SERIALIZATION_API ChildRange
    {
    public:
        constexpr ChildRange() noexcept = default;

        class Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = NodeView;
            using difference_type   = std::ptrdiff_t;

            [[nodiscard]] NodeView    operator*() const noexcept;
            Iterator&                 operator++() noexcept;
            [[nodiscard]] friend bool operator==(const Iterator&, const Iterator&) noexcept = default;

        private:
            friend class ChildRange;
            constexpr Iterator(const detail::DocumentState* state, UInt32 index) noexcept
                : m_state(state), m_index(index)
            {
            }
            const detail::DocumentState* m_state {nullptr};
            UInt32                       m_index {0};
        };

        [[nodiscard]] UIntSize Size() const noexcept { return m_count; }
        [[nodiscard]] bool     Empty() const noexcept { return m_count == 0; }
        [[nodiscard]] NodeView operator[](UIntSize index) const noexcept;
        [[nodiscard]] Iterator begin() const noexcept
        {
            return Iterator {
                    m_state,
                    m_count == 0 ? static_cast<UInt32>(-1) : m_begin,
            };
        }
        [[nodiscard]] Iterator end() const noexcept
        {
            return Iterator {m_state, static_cast<UInt32>(-1)};
        }

    private:
        friend class ElementView;
        constexpr ChildRange(const detail::DocumentState* state, UInt32 begin, UInt32 count) noexcept
            : m_state(state), m_begin(begin), m_count(count)
        {
        }
        const detail::DocumentState* m_state {nullptr};
        UInt32                       m_begin {0};
        UInt32                       m_count {0};
    };

    class NGIN_SERIALIZATION_API FilteredChildRange
    {
    public:
        class Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = ElementView;
            using difference_type   = std::ptrdiff_t;

            [[nodiscard]] ElementView operator*() const noexcept;
            Iterator&                 operator++() noexcept;
            [[nodiscard]] friend bool operator==(const Iterator&, const Iterator&) noexcept = default;

        private:
            friend class FilteredChildRange;
            Iterator(const detail::DocumentState* state,
                     UInt32                       index,
                     UInt32                       end,
                     std::string_view             name) noexcept;
            void Seek() noexcept;

            const detail::DocumentState* m_state {nullptr};
            UInt32                       m_index {0};
            UInt32                       m_end {0};
            std::string_view             m_name {};
        };

        [[nodiscard]] UIntSize                   Size() const noexcept;
        [[nodiscard]] bool                       Empty() const noexcept;
        [[nodiscard]] std::optional<ElementView> First() const noexcept;
        [[nodiscard]] Iterator                   begin() const noexcept { return Iterator {m_state, m_begin, m_end, m_name}; }
        [[nodiscard]] Iterator                   end() const noexcept
        {
            return Iterator {
                    m_state,
                    static_cast<UInt32>(-1),
                    static_cast<UInt32>(-1),
                    m_name,
            };
        }

    private:
        friend class ElementView;
        constexpr FilteredChildRange(const detail::DocumentState* state,
                                     UInt32                       begin,
                                     UInt32                       end,
                                     std::string_view             name) noexcept
            : m_state(state), m_begin(begin), m_end(end), m_name(name)
        {
        }
        const detail::DocumentState* m_state {nullptr};
        UInt32                       m_begin {0};
        UInt32                       m_end {0};
        std::string_view             m_name {};
    };

    class NGIN_SERIALIZATION_API ElementView
    {
    public:
        ElementView() noexcept = default;

        [[nodiscard]] bool                            IsValid() const noexcept;
        [[nodiscard]] std::string_view                Name() const noexcept;
        [[nodiscard]] SourceSpan                      Span() const noexcept;
        [[nodiscard]] AttributeRange                  Attributes() const noexcept;
        [[nodiscard]] std::optional<AttributeView>    Attribute(std::string_view attributeName) const noexcept;
        [[nodiscard]] ChildRange                      Children() const noexcept;
        [[nodiscard]] FilteredChildRange              Children(std::string_view elementName) const noexcept;
        [[nodiscard]] std::optional<ElementView>      FirstChild(std::string_view elementName) const noexcept;
        [[nodiscard]] std::optional<std::string_view> FirstText() const noexcept;

    private:
        friend class NodeView;
        friend class Document;
        friend class BorrowedDocument;
        friend class FilteredChildRange;
        friend struct detail::DocumentState;

        ElementView(const detail::DocumentState* state, UInt32 index) noexcept;

        const detail::DocumentState* m_state {nullptr};
        UInt32                       m_index {0};
    };

    class NGIN_SERIALIZATION_API Document
    {
    public:
        Document() noexcept;
        ~Document();
        Document(Document&&) noexcept;
        Document& operator=(Document&&) noexcept;
        Document(const Document&)            = delete;
        Document& operator=(const Document&) = delete;

        [[nodiscard]] bool             IsValid() const noexcept;
        [[nodiscard]] ElementView      Root() const noexcept;
        [[nodiscard]] std::string_view SourceText() const noexcept;
        [[nodiscard]] UIntSize         MemoryUsed() const noexcept;
        [[nodiscard]] UIntSize         MemoryCommitted() const noexcept;
        [[nodiscard]] UIntSize         PeakMemoryCommitted() const noexcept;
        [[nodiscard]] UIntSize         AllocationCount() const noexcept;
        [[nodiscard]] UIntSize         NodeCount() const noexcept;
        [[nodiscard]] UIntSize         ElementCount() const noexcept;
        [[nodiscard]] UIntSize         AttributeCount() const noexcept;

    private:
        friend class Parser;
        friend class Builder;
        friend struct detail::DocumentAccess;
        explicit Document(std::unique_ptr<detail::DocumentState> state) noexcept;
        std::unique_ptr<detail::DocumentState> m_state;
    };

    class NGIN_SERIALIZATION_API BorrowedDocument
    {
    public:
        BorrowedDocument() noexcept;
        ~BorrowedDocument();
        BorrowedDocument(BorrowedDocument&&) noexcept;
        BorrowedDocument& operator=(BorrowedDocument&&) noexcept;
        BorrowedDocument(const BorrowedDocument&)            = delete;
        BorrowedDocument& operator=(const BorrowedDocument&) = delete;

        [[nodiscard]] bool             IsValid() const noexcept;
        [[nodiscard]] ElementView      Root() const noexcept;
        [[nodiscard]] std::string_view SourceText() const noexcept;
        [[nodiscard]] UIntSize         MemoryUsed() const noexcept;
        [[nodiscard]] UIntSize         MemoryCommitted() const noexcept;
        [[nodiscard]] UIntSize         PeakMemoryCommitted() const noexcept;
        [[nodiscard]] UIntSize         AllocationCount() const noexcept;
        [[nodiscard]] UIntSize         NodeCount() const noexcept;
        [[nodiscard]] UIntSize         ElementCount() const noexcept;
        [[nodiscard]] UIntSize         AttributeCount() const noexcept;

    private:
        friend class Parser;
        friend struct detail::DocumentAccess;
        explicit BorrowedDocument(std::unique_ptr<detail::DocumentState> state) noexcept;
        std::unique_ptr<detail::DocumentState> m_state;
    };

    enum class SyntaxKind : UInt8
    {
        XmlDeclaration,
        StartTag,
        EndTag,
        Text,
        CData,
        Comment,
        ProcessingInstruction,
        Doctype,
    };

    struct SyntaxToken
    {
        SyntaxKind kind {SyntaxKind::Text};
        SourceSpan span {};
    };

    class NGIN_SERIALIZATION_API SyntaxDocument
    {
    public:
        SyntaxDocument() noexcept;
        ~SyntaxDocument();
        SyntaxDocument(SyntaxDocument&&) noexcept;
        SyntaxDocument& operator=(SyntaxDocument&&) noexcept;
        SyntaxDocument(const SyntaxDocument&)            = delete;
        SyntaxDocument& operator=(const SyntaxDocument&) = delete;

        [[nodiscard]] bool                         IsValid() const noexcept;
        [[nodiscard]] std::string_view             SourceText() const noexcept;
        [[nodiscard]] std::span<const SyntaxToken> Tokens() const noexcept;

    private:
        friend class Parser;
        explicit SyntaxDocument(std::unique_ptr<detail::SyntaxState> state) noexcept;
        std::unique_ptr<detail::SyntaxState> m_state;
    };
}// namespace NGIN::Serialization::XML

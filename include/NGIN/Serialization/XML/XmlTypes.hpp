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

    /// @brief Stable index identifying a node inside one XML document state.
    struct NodeId
    {
        UInt32 value {static_cast<UInt32>(-1)};

        /// @brief Returns whether the identifier refers to a document node.
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != static_cast<UInt32>(-1);
        }

        /// @brief Compares node identifiers by stored index.
        [[nodiscard]] friend constexpr bool operator==(NodeId, NodeId) noexcept = default;
    };

    /// @brief Semantic kind of an XML document node.
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

    /// @brief Immutable borrowed view of one XML node.
    class NGIN_SERIALIZATION_API NodeView
    {
    public:
        /// @brief Constructs an invalid node view.
        NodeView() noexcept = default;

        /// @brief Returns whether this view refers to a live document node.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the node's semantic kind.
        [[nodiscard]] NodeKind Kind() const noexcept;
        /// @brief Returns the source range covering the node.
        [[nodiscard]] SourceSpan Span() const noexcept;
        /// @brief Returns the element or processing-instruction name when applicable.
        [[nodiscard]] std::string_view Name() const noexcept;
        /// @brief Returns an element view when this node is an element.
        [[nodiscard]] std::optional<ElementView> TryElement() const noexcept;
        /// @brief Returns borrowed text for text-like node kinds.
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

    /// @brief Immutable borrowed view of one XML attribute.
    class NGIN_SERIALIZATION_API AttributeView
    {
    public:
        /// @brief Constructs an invalid attribute view.
        AttributeView() noexcept = default;

        /// @brief Returns whether this view refers to a live attribute.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the decoded attribute name.
        [[nodiscard]] std::string_view Name() const noexcept;
        /// @brief Returns the decoded attribute value.
        [[nodiscard]] std::string_view Value() const noexcept;
        /// @brief Returns the source range covering the complete attribute.
        [[nodiscard]] SourceSpan Span() const noexcept;
        /// @brief Returns the source range covering the attribute name.
        [[nodiscard]] SourceSpan NameSpan() const noexcept;
        /// @brief Returns the source range covering the attribute value.
        [[nodiscard]] SourceSpan ValueSpan() const noexcept;

    private:
        friend class ElementView;
        friend class AttributeRange;

        AttributeView(const detail::DocumentState* state, UInt32 index) noexcept;

        const detail::DocumentState* m_state {nullptr};
        UInt32                       m_index {0};
    };

    /// @brief Immutable contiguous range of attributes in source order.
    class NGIN_SERIALIZATION_API AttributeRange
    {
    public:
        /// @brief Constructs an empty invalid range.
        constexpr AttributeRange() noexcept = default;

        class NGIN_SERIALIZATION_API Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = AttributeView;
            using difference_type   = std::ptrdiff_t;

            /// @brief Returns the attribute at the current position.
            [[nodiscard]] AttributeView operator*() const noexcept;
            /// @brief Advances to the next attribute.
            Iterator& operator++() noexcept;
            /// @brief Compares iterator document and position state.
            [[nodiscard]] friend bool operator==(const Iterator&, const Iterator&) noexcept = default;

        private:
            friend class AttributeRange;
            constexpr Iterator(const detail::DocumentState* state, UInt32 index) noexcept
                : m_state(state), m_index(index)
            {
            }
            const detail::DocumentState* m_state {nullptr};
            UInt32                       m_index {0};
        };

        /// @brief Returns the number of attributes.
        [[nodiscard]] UIntSize Size() const noexcept { return m_count; }
        /// @brief Returns whether the range contains no attributes.
        [[nodiscard]] bool Empty() const noexcept { return m_count == 0; }
        /// @brief Returns the attribute at @p index.
        /// @pre @p index is less than Size().
        [[nodiscard]] AttributeView operator[](UIntSize index) const noexcept;
        /// @brief Returns an iterator to the first attribute.
        [[nodiscard]] Iterator begin() const noexcept { return Iterator {m_state, m_begin}; }
        /// @brief Returns the past-the-end iterator.
        [[nodiscard]] Iterator end() const noexcept { return Iterator {m_state, m_begin + m_count}; }

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

    /// @brief Immutable range of direct child nodes in source order.
    class NGIN_SERIALIZATION_API ChildRange
    {
    public:
        /// @brief Constructs an empty invalid range.
        constexpr ChildRange() noexcept = default;

        class NGIN_SERIALIZATION_API Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = NodeView;
            using difference_type   = std::ptrdiff_t;

            /// @brief Returns the node at the current child position.
            [[nodiscard]] NodeView operator*() const noexcept;
            /// @brief Advances to the next sibling node.
            Iterator& operator++() noexcept;
            /// @brief Compares iterator document and position state.
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

        /// @brief Returns the number of direct child nodes.
        [[nodiscard]] UIntSize Size() const noexcept { return m_count; }
        /// @brief Returns whether the range contains no child nodes.
        [[nodiscard]] bool Empty() const noexcept { return m_count == 0; }
        /// @brief Returns the child node at @p index.
        /// @pre @p index is less than Size().
        [[nodiscard]] NodeView operator[](UIntSize index) const noexcept;
        /// @brief Returns an iterator to the first child node.
        [[nodiscard]] Iterator begin() const noexcept
        {
            return Iterator {
                    m_state,
                    m_count == 0 ? static_cast<UInt32>(-1) : m_begin,
            };
        }
        /// @brief Returns the past-the-end iterator.
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

    /// @brief Lazy range of direct child elements matching a decoded name.
    class NGIN_SERIALIZATION_API FilteredChildRange
    {
    public:
        class NGIN_SERIALIZATION_API Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = ElementView;
            using difference_type   = std::ptrdiff_t;

            /// @brief Returns the matching element at the current position.
            [[nodiscard]] ElementView operator*() const noexcept;
            /// @brief Advances to the next matching child element.
            Iterator& operator++() noexcept;
            /// @brief Compares iterator document and position state.
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

        /// @brief Counts matching direct child elements.
        [[nodiscard]] UIntSize Size() const noexcept;
        /// @brief Returns whether no direct child element matches.
        [[nodiscard]] bool Empty() const noexcept;
        /// @brief Returns the first matching child element, if any.
        [[nodiscard]] std::optional<ElementView> First() const noexcept;
        /// @brief Returns an iterator to the first matching child element.
        [[nodiscard]] Iterator begin() const noexcept { return Iterator {m_state, m_begin, m_end, m_name}; }
        /// @brief Returns the past-the-end iterator.
        [[nodiscard]] Iterator end() const noexcept
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

    /// @brief Immutable borrowed view of one XML element.
    class NGIN_SERIALIZATION_API ElementView
    {
    public:
        /// @brief Constructs an invalid element view.
        ElementView() noexcept = default;

        /// @brief Returns whether this view refers to a live element.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the decoded element name.
        [[nodiscard]] std::string_view Name() const noexcept;
        /// @brief Returns the source range covering the complete element.
        [[nodiscard]] SourceSpan Span() const noexcept;
        /// @brief Returns the element attributes in source order.
        [[nodiscard]] AttributeRange Attributes() const noexcept;
        /// @brief Finds an attribute by decoded name.
        [[nodiscard]] std::optional<AttributeView> Attribute(std::string_view attributeName) const noexcept;
        /// @brief Returns all direct child nodes in source order.
        [[nodiscard]] ChildRange Children() const noexcept;
        /// @brief Returns direct child elements matching a decoded name.
        [[nodiscard]] FilteredChildRange Children(std::string_view elementName) const noexcept;
        /// @brief Returns the first direct child element matching a decoded name.
        [[nodiscard]] std::optional<ElementView> FirstChild(std::string_view elementName) const noexcept;
        /// @brief Returns the first direct text-like child value, if any.
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

    /// @brief Self-contained owning XML semantic document.
    class NGIN_SERIALIZATION_API Document
    {
    public:
        /// @brief Constructs an empty document.
        Document() noexcept;
        /// @brief Releases source text, semantic nodes, and arena storage.
        ~Document();
        /// @brief Transfers ownership from another document.
        Document(Document&&) noexcept;
        /// @brief Replaces this document with another document's state.
        Document& operator=(Document&&) noexcept;
        /// @brief Documents are non-copyable because views refer directly to owned state.
        Document(const Document&) = delete;
        /// @brief Documents are non-copy-assignable because views refer directly to owned state.
        Document& operator=(const Document&) = delete;

        /// @brief Returns whether the document contains parsed state.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the root element, valid while this document remains alive and unmoved-from.
        [[nodiscard]] ElementView Root() const noexcept;
        /// @brief Returns the owned source text used to create the document.
        [[nodiscard]] std::string_view SourceText() const noexcept;
        /// @brief Returns bytes currently used by document arenas.
        [[nodiscard]] UIntSize MemoryUsed() const noexcept;
        /// @brief Returns bytes currently committed by document arenas.
        [[nodiscard]] UIntSize MemoryCommitted() const noexcept;
        /// @brief Returns the peak committed arena size observed while parsing or building.
        [[nodiscard]] UIntSize PeakMemoryCommitted() const noexcept;
        /// @brief Returns the number of arena allocation operations.
        [[nodiscard]] UIntSize AllocationCount() const noexcept;
        /// @brief Returns the number of stored semantic nodes.
        [[nodiscard]] UIntSize NodeCount() const noexcept;
        /// @brief Returns the number of stored elements.
        [[nodiscard]] UIntSize ElementCount() const noexcept;
        /// @brief Returns the number of stored attributes.
        [[nodiscard]] UIntSize AttributeCount() const noexcept;

    private:
        friend class Parser;
        friend class Builder;
        friend struct detail::DocumentAccess;
        explicit Document(std::unique_ptr<detail::DocumentState> state) noexcept;
        std::unique_ptr<detail::DocumentState> m_state;
    };

    /// @brief XML semantic document whose source storage remains owned by the caller.
    class NGIN_SERIALIZATION_API BorrowedDocument
    {
    public:
        /// @brief Constructs an empty borrowed document.
        BorrowedDocument() noexcept;
        /// @brief Releases parsed state without releasing caller-owned source storage.
        ~BorrowedDocument();
        /// @brief Transfers parsed state and its source borrowing relationship.
        BorrowedDocument(BorrowedDocument&&) noexcept;
        /// @brief Replaces this state with another borrowed document's state.
        BorrowedDocument& operator=(BorrowedDocument&&) noexcept;
        /// @brief Borrowed documents are non-copyable because views refer directly to state.
        BorrowedDocument(const BorrowedDocument&) = delete;
        /// @brief Borrowed documents are non-copy-assignable because views refer directly to state.
        BorrowedDocument& operator=(const BorrowedDocument&) = delete;

        /// @brief Returns whether the document contains parsed state.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the root element.
        /// @note The caller-owned source and this document must outlive every returned view.
        [[nodiscard]] ElementView Root() const noexcept;
        /// @brief Returns a view of the caller-owned source text.
        [[nodiscard]] std::string_view SourceText() const noexcept;
        /// @brief Returns bytes currently used by document arenas.
        [[nodiscard]] UIntSize MemoryUsed() const noexcept;
        /// @brief Returns bytes currently committed by document arenas.
        [[nodiscard]] UIntSize MemoryCommitted() const noexcept;
        /// @brief Returns the peak committed arena size observed while parsing.
        [[nodiscard]] UIntSize PeakMemoryCommitted() const noexcept;
        /// @brief Returns the number of arena allocation operations.
        [[nodiscard]] UIntSize AllocationCount() const noexcept;
        /// @brief Returns the number of stored semantic nodes.
        [[nodiscard]] UIntSize NodeCount() const noexcept;
        /// @brief Returns the number of stored elements.
        [[nodiscard]] UIntSize ElementCount() const noexcept;
        /// @brief Returns the number of stored attributes.
        [[nodiscard]] UIntSize AttributeCount() const noexcept;

    private:
        friend class Parser;
        friend struct detail::DocumentAccess;
        explicit BorrowedDocument(std::unique_ptr<detail::DocumentState> state) noexcept;
        std::unique_ptr<detail::DocumentState> m_state;
    };

    /// @brief Kind of one source-preserving XML syntax token.
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

    /// @brief Source range and kind of one XML syntax construct.
    struct SyntaxToken
    {
        SyntaxKind kind {SyntaxKind::Text};
        SourceSpan span {};
    };

    /// @brief Owning source-preserving XML document represented as syntax tokens.
    class NGIN_SERIALIZATION_API SyntaxDocument
    {
    public:
        /// @brief Constructs an empty syntax document.
        SyntaxDocument() noexcept;
        /// @brief Releases owned source and syntax-token storage.
        ~SyntaxDocument();
        /// @brief Transfers ownership from another syntax document.
        SyntaxDocument(SyntaxDocument&&) noexcept;
        /// @brief Replaces this syntax document with another document's state.
        SyntaxDocument& operator=(SyntaxDocument&&) noexcept;
        /// @brief Syntax documents are non-copyable because token spans refer to owned source.
        SyntaxDocument(const SyntaxDocument&) = delete;
        /// @brief Syntax documents are non-copy-assignable because token spans refer to owned source.
        SyntaxDocument& operator=(const SyntaxDocument&) = delete;

        /// @brief Returns whether the document contains parsed syntax state.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the owned source text.
        [[nodiscard]] std::string_view SourceText() const noexcept;
        /// @brief Returns syntax tokens in source order.
        /// @note The span remains valid only while this document is alive and unmoved-from.
        [[nodiscard]] std::span<const SyntaxToken> Tokens() const noexcept;

    private:
        friend class Parser;
        explicit SyntaxDocument(std::unique_ptr<detail::SyntaxState> state) noexcept;
        std::unique_ptr<detail::SyntaxState> m_state;
    };
}// namespace NGIN::Serialization::XML

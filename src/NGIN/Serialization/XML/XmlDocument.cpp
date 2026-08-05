#include <NGIN/Serialization/XML/XmlTypes.hpp>

#include "XmlDocumentInternal.hpp"

namespace NGIN::Serialization::XML
{
    namespace
    {
        [[nodiscard]] const detail::NodeRecord*
        Resolve(const detail::DocumentState* state, NodeId id) noexcept
        {
            return state ? state->Node(id) : nullptr;
        }

        [[nodiscard]] UInt32 ElementIndex(const detail::NodeRecord& node) noexcept
        {
            return node.name.offsetOrId;
        }
    }// namespace

    NodeView::NodeView(const detail::DocumentState* state, NodeId id) noexcept
        : m_state(state), m_id(id)
    {
    }

    AttributeView::AttributeView(const detail::DocumentState* state, UInt32 index) noexcept
        : m_state(state), m_index(index)
    {
    }

    ElementView::ElementView(const detail::DocumentState* state, UInt32 index) noexcept
        : m_state(state), m_index(index)
    {
    }

    bool NodeView::IsValid() const noexcept
    { return Resolve(m_state, m_id) != nullptr; }
    NodeKind NodeView::Kind() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node ? node->kind : NodeKind::Text;
    }
    SourceSpan NodeView::Span() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node ? m_state->ExpandSpan(node->span) : SourceSpan {};
    }
    std::string_view NodeView::Name() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        if (!node)
            return {};
        const auto elementIndex = ElementIndex(*node);
        if (node->kind == NodeKind::Element && elementIndex < m_state->elements.size())
            return m_state->Text(m_state->elements[elementIndex].name);
        return m_state->Text(node->name);
    }
    std::optional<ElementView> NodeView::TryElement() const noexcept
    {
        const auto* node         = Resolve(m_state, m_id);
        const auto  elementIndex = node ? ElementIndex(*node) : 0;
        return node && node->kind == NodeKind::Element && elementIndex < m_state->elements.size()
                       ? std::optional<ElementView> {ElementView {m_state, elementIndex}}
                       : std::nullopt;
    }
    std::optional<std::string_view> NodeView::TryText() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node && node->kind != NodeKind::Element
                       ? std::optional<std::string_view> {m_state->Text(node->text)}
                       : std::nullopt;
    }

    bool AttributeView::IsValid() const noexcept
    {
        return m_state && m_index < m_state->attributes.size();
    }
    std::string_view AttributeView::Name() const noexcept
    {
        return IsValid() ? m_state->Text(m_state->attributes[m_index].name) : std::string_view {};
    }
    std::string_view AttributeView::Value() const noexcept
    {
        return IsValid() ? m_state->Text(m_state->attributes[m_index].value) : std::string_view {};
    }
    SourceSpan AttributeView::Span() const noexcept
    {
        return IsValid() ? m_state->ExpandSpan(m_state->attributes[m_index].span) : SourceSpan {};
    }
    SourceSpan AttributeView::NameSpan() const noexcept
    {
        return IsValid() ? m_state->TextSpan(m_state->attributes[m_index].name) : SourceSpan {};
    }
    SourceSpan AttributeView::ValueSpan() const noexcept
    {
        return IsValid() ? m_state->ExpandSpan(m_state->attributes[m_index].valueSpan) : SourceSpan {};
    }

    AttributeView AttributeRange::Iterator::operator*() const noexcept
    {
        return AttributeView {m_state, m_index};
    }
    AttributeRange::Iterator& AttributeRange::Iterator::operator++() noexcept
    {
        ++m_index;
        return *this;
    }
    AttributeView AttributeRange::operator[](UIntSize index) const noexcept
    {
        return index < m_count
                       ? AttributeView {m_state, static_cast<UInt32>(m_begin + index)}
                       : AttributeView {};
    }

    NodeView ChildRange::Iterator::operator*() const noexcept
    {
        return NodeView {m_state, NodeId {m_index}};
    }
    ChildRange::Iterator& ChildRange::Iterator::operator++() noexcept
    {
        const auto* node = Resolve(m_state, NodeId {m_index});
        m_index          = node ? node->nextSibling : static_cast<UInt32>(-1);
        return *this;
    }
    NodeView ChildRange::operator[](UIntSize index) const noexcept
    {
        if (!m_state || index >= m_count)
            return {};
        auto id = NodeId {m_begin};
        while (index-- > 0)
        {
            const auto* node = Resolve(m_state, id);
            if (!node)
                return {};
            id.value = node->nextSibling;
        }
        return NodeView {m_state, id};
    }

    FilteredChildRange::Iterator::Iterator(const detail::DocumentState* state,
                                           UInt32                       index,
                                           UInt32                       end,
                                           std::string_view             name) noexcept
        : m_state(state), m_index(index), m_end(end), m_name(name)
    {
        Seek();
    }
    void FilteredChildRange::Iterator::Seek() noexcept
    {
        while (m_state && m_index != static_cast<UInt32>(-1))
        {
            const auto* node         = m_state->Node(NodeId {m_index});
            const auto  elementIndex = node ? ElementIndex(*node) : 0;
            if (node && node->kind == NodeKind::Element &&
                elementIndex < m_state->elements.size() &&
                (m_name.empty() || m_state->Text(m_state->elements[elementIndex].name) == m_name))
                return;
            m_index = node ? node->nextSibling : static_cast<UInt32>(-1);
        }
        m_index = static_cast<UInt32>(-1);
    }
    ElementView FilteredChildRange::Iterator::operator*() const noexcept
    {
        const auto* node = m_state ? m_state->Node(NodeId {m_index}) : nullptr;
        return node && node->kind == NodeKind::Element
                       ? ElementView {m_state, ElementIndex(*node)}
                       : ElementView {};
    }
    FilteredChildRange::Iterator& FilteredChildRange::Iterator::operator++() noexcept
    {
        const auto* node = m_state ? m_state->Node(NodeId {m_index}) : nullptr;
        m_index          = node ? node->nextSibling : static_cast<UInt32>(-1);
        Seek();
        return *this;
    }
    UIntSize FilteredChildRange::Size() const noexcept
    {
        UIntSize count = 0;
        for (auto iterator = begin(); iterator != end(); ++iterator)
            ++count;
        return count;
    }
    bool FilteredChildRange::Empty() const noexcept
    {
        return begin() == end();
    }
    std::optional<ElementView> FilteredChildRange::First() const noexcept
    {
        const auto first = begin();
        return first != end() ? std::optional<ElementView> {*first} : std::nullopt;
    }

    bool ElementView::IsValid() const noexcept
    { return m_state && m_index < m_state->elements.size(); }
    std::string_view ElementView::Name() const noexcept
    {
        return IsValid() ? m_state->Text(m_state->elements[m_index].name) : std::string_view {};
    }
    SourceSpan ElementView::Span() const noexcept
    {
        return IsValid() ? m_state->ExpandSpan(m_state->elements[m_index].span) : SourceSpan {};
    }
    AttributeRange ElementView::Attributes() const noexcept
    {
        if (!IsValid())
            return AttributeRange {nullptr, 0, 0};
        const auto range = m_state->elements[m_index].attributes;
        return AttributeRange {m_state, range.begin, range.count};
    }
    std::optional<AttributeView> ElementView::Attribute(std::string_view attributeName) const noexcept
    {
        for (const AttributeView attribute: Attributes())
        {
            if (attribute.Name() == attributeName)
                return attribute;
        }
        return std::nullopt;
    }
    ChildRange ElementView::Children() const noexcept
    {
        if (!IsValid())
            return ChildRange {nullptr, 0, 0};
        const auto range = m_state->elements[m_index].children;
        return ChildRange {m_state, range.first, range.count};
    }
    FilteredChildRange ElementView::Children(std::string_view elementName) const noexcept
    {
        if (!IsValid())
            return FilteredChildRange {nullptr, 0, 0, elementName};
        const auto range = m_state->elements[m_index].children;
        const auto begin = range.first;
        return FilteredChildRange {
                m_state,
                begin,
                static_cast<UInt32>(-1),
                elementName,
        };
    }
    std::optional<ElementView> ElementView::FirstChild(std::string_view elementName) const noexcept
    {
        const auto range = Children(elementName);
        const auto first = range.begin();
        if (first != range.end())
            return *first;
        return std::nullopt;
    }
    std::optional<std::string_view> ElementView::FirstText() const noexcept
    {
        for (const NodeView child: Children())
        {
            if (child.Kind() == NodeKind::Text || child.Kind() == NodeKind::CData)
                return child.TryText();
        }
        return std::nullopt;
    }

    Document::Document() noexcept                      = default;
    Document::~Document()                              = default;
    Document::Document(Document&&) noexcept            = default;
    Document& Document::operator=(Document&&) noexcept = default;
    Document::Document(std::unique_ptr<detail::DocumentState> state) noexcept
        : m_state(std::move(state))
    {
    }
    bool Document::IsValid() const noexcept
    {
        const auto* root = m_state ? m_state->Node(m_state->root) : nullptr;
        return root && root->kind == NodeKind::Element;
    }
    ElementView Document::Root() const noexcept
    {
        const auto* root = IsValid() ? m_state->Node(m_state->root) : nullptr;
        return root ? ElementView {m_state.get(), ElementIndex(*root)} : ElementView {};
    }
    std::string_view Document::SourceText() const noexcept
    {
        return m_state ? m_state->source : std::string_view {};
    }
    UIntSize Document::MemoryUsed() const noexcept
    { return m_state ? m_state->MemoryUsed() : 0; }
    UIntSize Document::MemoryCommitted() const noexcept
    { return m_state ? m_state->MemoryCommitted() : 0; }
    UIntSize Document::PeakMemoryCommitted() const noexcept
    { return m_state ? m_state->PeakMemoryCommitted() : 0; }
    UIntSize Document::AllocationCount() const noexcept
    { return m_state ? m_state->budget.AllocationCount() : 0; }
    UIntSize Document::NodeCount() const noexcept
    { return m_state ? m_state->nodes.size() : 0; }
    UIntSize Document::ElementCount() const noexcept
    { return m_state ? m_state->elements.size() : 0; }
    UIntSize Document::AttributeCount() const noexcept
    { return m_state ? m_state->attributes.size() : 0; }

    BorrowedDocument::BorrowedDocument() noexcept                              = default;
    BorrowedDocument::~BorrowedDocument()                                      = default;
    BorrowedDocument::BorrowedDocument(BorrowedDocument&&) noexcept            = default;
    BorrowedDocument& BorrowedDocument::operator=(BorrowedDocument&&) noexcept = default;
    BorrowedDocument::BorrowedDocument(std::unique_ptr<detail::DocumentState> state) noexcept
        : m_state(std::move(state))
    {
    }
    bool BorrowedDocument::IsValid() const noexcept
    {
        const auto* root = m_state ? m_state->Node(m_state->root) : nullptr;
        return root && root->kind == NodeKind::Element;
    }
    ElementView BorrowedDocument::Root() const noexcept
    {
        const auto* root = IsValid() ? m_state->Node(m_state->root) : nullptr;
        return root ? ElementView {m_state.get(), ElementIndex(*root)} : ElementView {};
    }
    std::string_view BorrowedDocument::SourceText() const noexcept
    {
        return m_state ? m_state->source : std::string_view {};
    }
    UIntSize BorrowedDocument::MemoryUsed() const noexcept
    { return m_state ? m_state->MemoryUsed() : 0; }
    UIntSize BorrowedDocument::MemoryCommitted() const noexcept
    { return m_state ? m_state->MemoryCommitted() : 0; }
    UIntSize BorrowedDocument::PeakMemoryCommitted() const noexcept
    { return m_state ? m_state->PeakMemoryCommitted() : 0; }
    UIntSize BorrowedDocument::AllocationCount() const noexcept
    { return m_state ? m_state->budget.AllocationCount() : 0; }
    UIntSize BorrowedDocument::NodeCount() const noexcept
    { return m_state ? m_state->nodes.size() : 0; }
    UIntSize BorrowedDocument::ElementCount() const noexcept
    { return m_state ? m_state->elements.size() : 0; }
    UIntSize BorrowedDocument::AttributeCount() const noexcept
    { return m_state ? m_state->attributes.size() : 0; }

    SyntaxDocument::SyntaxDocument() noexcept                            = default;
    SyntaxDocument::~SyntaxDocument()                                    = default;
    SyntaxDocument::SyntaxDocument(SyntaxDocument&&) noexcept            = default;
    SyntaxDocument& SyntaxDocument::operator=(SyntaxDocument&&) noexcept = default;
    SyntaxDocument::SyntaxDocument(std::unique_ptr<detail::SyntaxState> state) noexcept
        : m_state(std::move(state))
    {
    }
    bool SyntaxDocument::IsValid() const noexcept
    { return m_state && m_state->valid; }
    std::string_view SyntaxDocument::SourceText() const noexcept
    {
        return m_state ? m_state->source.View() : std::string_view {};
    }
    std::span<const SyntaxToken> SyntaxDocument::Tokens() const noexcept
    {
        return m_state ? std::span<const SyntaxToken> {m_state->tokens} : std::span<const SyntaxToken> {};
    }
}// namespace NGIN::Serialization::XML

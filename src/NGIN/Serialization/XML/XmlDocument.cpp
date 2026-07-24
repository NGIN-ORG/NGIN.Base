#include <NGIN/Serialization/XML/XmlTypes.hpp>

#include "XmlDocumentInternal.hpp"

namespace NGIN::Serialization::XML
{
    void detail::DocumentState::FinalizeViews()
    {
        elementViews.clear();
        elementViews.reserve(elements.size());
        for (UInt32 index = 0; index < elements.size(); ++index)
            elementViews.push_back(ElementView {this, index});
    }

    namespace
    {
        [[nodiscard]] const detail::NodeRecord*
        Resolve(const detail::DocumentState* state, NodeId id) noexcept
        {
            return state ? state->Node(id) : nullptr;
        }
    }// namespace

    NodeView::NodeView(const detail::DocumentState* state, NodeId id) noexcept
        : m_state(state), m_id(id)
    {
        const auto* node = Resolve(state, id);
        if (!node)
            return;
        type = static_cast<Type>(node->kind);
        text = node->text.View();
        if (node->kind == NodeKind::Element && node->element < state->elementViews.size())
            element = &state->elementViews[node->element];
    }

    AttributeView::AttributeView(const detail::DocumentState* state, UInt32 index) noexcept
        : m_state(state), m_index(index)
    {
        if (state && index < state->attributes.size())
        {
            name  = state->attributes[index].name.View();
            value = state->attributes[index].value.View();
        }
    }

    ElementView::ElementView(const detail::DocumentState* state, UInt32 index) noexcept
        : m_state(state), m_index(index)
    {
        if (state && index < state->elements.size())
        {
            const auto& record = state->elements[index];
            name               = record.name.View();
            attributes         = AttributeRange {state, record.attributes.begin, record.attributes.count};
            children           = ChildRange {state, record.children.begin, record.children.count};
        }
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
        return node ? node->span : SourceSpan {};
    }
    std::string_view NodeView::Name() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        if (!node)
            return {};
        if (node->kind == NodeKind::Element && node->element < m_state->elements.size())
            return m_state->elements[node->element].name.View();
        return node->name.View();
    }
    std::optional<ElementView> NodeView::TryElement() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node && node->kind == NodeKind::Element && node->element < m_state->elements.size()
                       ? std::optional<ElementView> {ElementView {m_state, node->element}}
                       : std::nullopt;
    }
    const ElementView* NodeView::ElementPtr() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node && node->kind == NodeKind::Element &&
                               node->element < m_state->elementViews.size()
                       ? &m_state->elementViews[node->element]
                       : nullptr;
    }
    std::optional<std::string_view> NodeView::TryText() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node && node->kind != NodeKind::Element
                       ? std::optional<std::string_view> {node->text.View()}
                       : std::nullopt;
    }

    bool AttributeView::IsValid() const noexcept
    {
        return m_state && m_index < m_state->attributes.size();
    }
    std::string_view AttributeView::Name() const noexcept
    {
        return IsValid() ? m_state->attributes[m_index].name.View() : std::string_view {};
    }
    std::string_view AttributeView::Value() const noexcept
    {
        return IsValid() ? m_state->attributes[m_index].value.View() : std::string_view {};
    }
    SourceSpan AttributeView::Span() const noexcept
    {
        return IsValid() ? m_state->attributes[m_index].span : SourceSpan {};
    }
    SourceSpan AttributeView::NameSpan() const noexcept
    {
        return IsValid() ? m_state->attributes[m_index].nameSpan : SourceSpan {};
    }
    SourceSpan AttributeView::ValueSpan() const noexcept
    {
        return IsValid() ? m_state->attributes[m_index].valueSpan : SourceSpan {};
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
        return m_state && m_index < m_state->children.size()
                       ? NodeView {m_state, m_state->children[m_index]}
                       : NodeView {};
    }
    ChildRange::Iterator& ChildRange::Iterator::operator++() noexcept
    {
        ++m_index;
        return *this;
    }
    NodeView ChildRange::operator[](UIntSize index) const noexcept
    {
        return m_state && index < m_count && m_begin + index < m_state->children.size()
                       ? NodeView {m_state, m_state->children[m_begin + index]}
                       : NodeView {};
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
        while (m_state && m_index < m_end && m_index < m_state->children.size())
        {
            const auto* node = m_state->Node(m_state->children[m_index]);
            if (node && node->kind == NodeKind::Element &&
                node->element < m_state->elements.size() &&
                (m_name.empty() || m_state->elements[node->element].name.View() == m_name))
                return;
            ++m_index;
        }
        m_index = m_end;
    }
    ElementView FilteredChildRange::Iterator::operator*() const noexcept
    {
        if (!m_state || m_index >= m_state->children.size())
            return {};
        const auto* node = m_state->Node(m_state->children[m_index]);
        return node && node->kind == NodeKind::Element ? ElementView {m_state, node->element} : ElementView {};
    }
    FilteredChildRange::Iterator& FilteredChildRange::Iterator::operator++() noexcept
    {
        ++m_index;
        Seek();
        return *this;
    }

    bool ElementView::IsValid() const noexcept
    { return m_state && m_index < m_state->elements.size(); }
    std::string_view ElementView::Name() const noexcept
    {
        return IsValid() ? m_state->elements[m_index].name.View() : std::string_view {};
    }
    SourceSpan ElementView::Span() const noexcept
    {
        return IsValid() ? m_state->elements[m_index].span : SourceSpan {};
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
        for (const auto attribute: Attributes())
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
        return ChildRange {m_state, range.begin, range.count};
    }
    FilteredChildRange ElementView::Children(std::string_view elementName) const noexcept
    {
        if (!IsValid())
            return FilteredChildRange {nullptr, 0, 0, elementName};
        const auto range = m_state->elements[m_index].children;
        return FilteredChildRange {m_state, range.begin, range.begin + range.count, elementName};
    }
    std::optional<ElementView> ElementView::FirstChild(std::string_view elementName) const noexcept
    {
        const auto range = Children(elementName);
        const auto first = range.begin();
        if (first != range.end())
            return *first;
        return std::nullopt;
    }
    const ElementView* ElementView::FirstChildPtr(std::string_view elementName) const noexcept
    {
        for (const auto child: Children())
        {
            const auto* element = child.ElementPtr();
            if (element && element->Name() == elementName)
                return element;
        }
        return nullptr;
    }
    std::optional<std::string_view> ElementView::FirstText() const noexcept
    {
        for (const auto child: Children())
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
        return root ? ElementView {m_state.get(), root->element} : ElementView {};
    }
    const ElementView* Document::RootPtr() const noexcept
    {
        const auto* root = IsValid() ? m_state->Node(m_state->root) : nullptr;
        return root && root->element < m_state->elementViews.size()
                       ? &m_state->elementViews[root->element]
                       : nullptr;
    }
    std::string_view Document::SourceText() const noexcept
    {
        return m_state ? m_state->source : std::string_view {};
    }
    UIntSize Document::MemoryUsed() const noexcept
    { return m_state ? m_state->MemoryUsed() : 0; }
    UIntSize Document::MemoryCommitted() const noexcept
    { return m_state ? m_state->MemoryCommitted() : 0; }
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
        return root ? ElementView {m_state.get(), root->element} : ElementView {};
    }
    const ElementView* BorrowedDocument::RootPtr() const noexcept
    {
        const auto* root = IsValid() ? m_state->Node(m_state->root) : nullptr;
        return root && root->element < m_state->elementViews.size()
                       ? &m_state->elementViews[root->element]
                       : nullptr;
    }
    std::string_view BorrowedDocument::SourceText() const noexcept
    {
        return m_state ? m_state->source : std::string_view {};
    }
    UIntSize BorrowedDocument::MemoryUsed() const noexcept
    { return m_state ? m_state->MemoryUsed() : 0; }
    UIntSize BorrowedDocument::MemoryCommitted() const noexcept
    { return m_state ? m_state->MemoryCommitted() : 0; }
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

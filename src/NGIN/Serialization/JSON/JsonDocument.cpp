#include <NGIN/Serialization/JSON/JsonTypes.hpp>

#include "JsonDocumentInternal.hpp"

#include <algorithm>
#include <limits>

namespace NGIN::Serialization::JSON
{
    UInt32 detail::DocumentState::FindMember(UIntSize begin, UIntSize count, std::string_view key) const noexcept
    {
        if (count >= NameIndex::Threshold)
        {
            const auto found = std::lower_bound(indexes.begin(), indexes.end(), begin,
                                                [](const IndexedNames& entry, UIntSize offset) { return entry.begin < offset; });
            if (found != indexes.end() && found->begin == begin)
                return found->index.Find(key, count, [&](UIntSize i) { return members[begin + i].key.View(); });
        }
        for (UIntSize i = 0; i < count; ++i)
            if (members[begin + i].key.View() == key)
                return static_cast<UInt32>(i);
        return NameIndex::Missing;
    }

    namespace
    {
        [[nodiscard]] const detail::NodeRecord* Resolve(const detail::DocumentState* state, NodeId id) noexcept
        {
            return state ? state->Node(id) : nullptr;
        }
    }// namespace

    bool ValueView::IsValid() const noexcept
    {
        return Resolve(m_state, m_id) != nullptr;
    }

    ValueKind ValueView::Kind() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node ? node->kind : ValueKind::Null;
    }

    SourceSpan ValueView::Span() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node ? m_state->ExpandSpan(node->span) : SourceSpan {};
    }

    bool ValueView::IsNull() const noexcept { return IsValid() && Kind() == ValueKind::Null; }
    bool ValueView::IsBool() const noexcept { return IsValid() && Kind() == ValueKind::Bool; }
    bool ValueView::IsInt64() const noexcept { return IsValid() && Kind() == ValueKind::Int64; }
    bool ValueView::IsUInt64() const noexcept { return IsValid() && Kind() == ValueKind::UInt64; }
    bool ValueView::IsDouble() const noexcept { return IsValid() && Kind() == ValueKind::Double; }
    bool ValueView::IsNumber() const noexcept
    {
        const auto kind = Kind();
        return IsValid() && (kind == ValueKind::Int64 || kind == ValueKind::UInt64 || kind == ValueKind::Double);
    }
    bool ValueView::IsString() const noexcept { return IsValid() && Kind() == ValueKind::String; }
    bool ValueView::IsArray() const noexcept { return IsValid() && Kind() == ValueKind::Array; }
    bool ValueView::IsObject() const noexcept { return IsValid() && Kind() == ValueKind::Object; }

    std::optional<bool> ValueView::TryBool() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node && node->kind == ValueKind::Bool
                     ? std::optional<bool> {node->payload.boolValue}
                     : std::nullopt;
    }

    std::optional<Int64> ValueView::TryInt64() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        if (!node)
            return std::nullopt;
        if (node->kind == ValueKind::Int64)
            return node->payload.signedValue;
        if (node->kind == ValueKind::UInt64 &&
            node->payload.unsignedValue <= static_cast<UInt64>((std::numeric_limits<Int64>::max)()))
            return static_cast<Int64>(node->payload.unsignedValue);
        return std::nullopt;
    }

    std::optional<UInt64> ValueView::TryUInt64() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        if (!node)
            return std::nullopt;
        if (node->kind == ValueKind::UInt64)
            return node->payload.unsignedValue;
        if (node->kind == ValueKind::Int64 && node->payload.signedValue >= 0)
            return static_cast<UInt64>(node->payload.signedValue);
        return std::nullopt;
    }

    std::optional<F64> ValueView::TryDouble() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        if (!node)
            return std::nullopt;
        switch (node->kind)
        {
            case ValueKind::Int64:
                return static_cast<F64>(node->payload.signedValue);
            case ValueKind::UInt64:
                return static_cast<F64>(node->payload.unsignedValue);
            case ValueKind::Double:
                return node->payload.doubleValue;
            default:
                return std::nullopt;
        }
    }

    std::optional<std::string_view> ValueView::TryString() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node && node->kind == ValueKind::String
                     ? std::optional<std::string_view> {node->payload.stringValue.View()}
                     : std::nullopt;
    }

    std::optional<ArrayView> ValueView::TryArray() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node && node->kind == ValueKind::Array
                       ? std::optional<ArrayView> {
                                 ArrayView {m_state,
                                            node->payload.rangeValue.begin,
                                            node->payload.rangeValue.count,
                                            m_state->ExpandSpan(node->span)}}
                       : std::nullopt;
    }

    std::optional<ObjectView> ValueView::TryObject() const noexcept
    {
        const auto* node = Resolve(m_state, m_id);
        return node && node->kind == ValueKind::Object
                       ? std::optional<ObjectView> {
                                 ObjectView {m_state,
                                             node->payload.rangeValue.begin,
                                             node->payload.rangeValue.count,
                                             m_state->ExpandSpan(node->span)}}
                       : std::nullopt;
    }

    bool MemberView::IsValid() const noexcept
    {
        return m_state && m_index < m_state->members.size();
    }

    std::string_view MemberView::Key() const noexcept
    {
        return IsValid() ? m_state->members[m_index].key.View() : std::string_view {};
    }

    ValueView MemberView::Value() const noexcept
    {
        return IsValid() ? ValueView {m_state, m_state->members[m_index].value} : ValueView {};
    }

    SourceSpan MemberView::Span() const noexcept
    {
        return IsValid() ? m_state->ExpandSpan(m_state->members[m_index].span) : SourceSpan {};
    }

    ValueView ArrayView::Iterator::operator*() const noexcept
    {
        if (!m_state || m_index >= m_state->elements.size())
            return {};
        return ValueView {m_state, m_state->elements[m_index]};
    }

    ArrayView::Iterator& ArrayView::Iterator::operator++() noexcept
    {
        ++m_index;
        return *this;
    }

    ArrayView::Iterator ArrayView::Iterator::operator++(int) noexcept
    {
        auto copy = *this;
        ++*this;
        return copy;
    }

    bool ArrayView::IsValid() const noexcept
    {
        return m_state && m_begin <= m_state->elements.size() &&
               m_count <= m_state->elements.size() - m_begin;
    }

    UIntSize ArrayView::Size() const noexcept
    {
        return IsValid() ? m_count : 0;
    }

    ValueView ArrayView::operator[](UIntSize index) const noexcept
    {
        return IsValid() && index < m_count
                     ? ValueView {m_state, m_state->elements[m_begin + index]}
                     : ValueView {};
    }

    ArrayView::Iterator ArrayView::begin() const noexcept
    {
        return IsValid() ? Iterator {m_state, m_begin} : Iterator {};
    }

    ArrayView::Iterator ArrayView::end() const noexcept
    {
        return IsValid() ? Iterator {m_state, m_begin + m_count} : Iterator {};
    }

    SourceSpan ArrayView::Span() const noexcept
    {
        return IsValid() ? m_span : SourceSpan {};
    }

    MemberView ObjectView::Iterator::operator*() const noexcept
    {
        return m_state && m_index < m_state->members.size()
                     ? MemberView {m_state, m_index}
                     : MemberView {};
    }

    ObjectView::Iterator& ObjectView::Iterator::operator++() noexcept
    {
        ++m_index;
        return *this;
    }

    ObjectView::Iterator ObjectView::Iterator::operator++(int) noexcept
    {
        auto copy = *this;
        ++*this;
        return copy;
    }

    bool ObjectView::IsValid() const noexcept
    {
        return m_state && m_begin <= m_state->members.size() &&
               m_count <= m_state->members.size() - m_begin;
    }

    UIntSize ObjectView::Size() const noexcept
    {
        return IsValid() ? m_count : 0;
    }

    MemberView ObjectView::MemberAt(UIntSize index) const noexcept
    {
        return IsValid() && index < m_count
                     ? MemberView {m_state, m_begin + index}
                     : MemberView {};
    }

    std::optional<ValueView> ObjectView::Find(std::string_view key) const noexcept
    {
        if (!IsValid())
            return std::nullopt;
        if (m_count < detail::NameIndex::Threshold)
        {
            for (UIntSize i = m_begin; i < m_begin + m_count; ++i)
                if (m_state->members[i].key.View() == key)
                    return ValueView {m_state, m_state->members[i].value};
            return std::nullopt;
        }
        const UInt32 index = m_state->FindMember(m_begin, m_count, key);
        return index == detail::NameIndex::Missing ? std::nullopt
                                                   : std::optional<ValueView> {ValueView {m_state, m_state->members[m_begin + index].value}};
    }

    ObjectView::Iterator ObjectView::begin() const noexcept
    {
        return IsValid() ? Iterator {m_state, m_begin} : Iterator {};
    }

    ObjectView::Iterator ObjectView::end() const noexcept
    {
        return IsValid() ? Iterator {m_state, m_begin + m_count} : Iterator {};
    }

    SourceSpan ObjectView::Span() const noexcept
    {
        return IsValid() ? m_span : SourceSpan {};
    }

    Document::Document() noexcept = default;
    Document::~Document()         = default;
    Document::Document(Document&&) noexcept = default;
    Document& Document::operator=(Document&&) noexcept = default;

    Document::Document(std::unique_ptr<detail::DocumentState> state) noexcept
        : m_state(std::move(state))
    {
    }

    bool Document::IsValid() const noexcept
    {
        return m_state && m_state->Node(m_state->root);
    }

    ValueView Document::Root() const noexcept
    {
        return IsValid() ? ValueView {m_state.get(), m_state->root} : ValueView {};
    }

    std::string_view Document::SourceText() const noexcept
    {
        return m_state ? m_state->source : std::string_view {};
    }

    UIntSize Document::MemoryUsed() const noexcept
    {
        return m_state ? m_state->MemoryUsed() : 0;
    }

    UIntSize Document::MemoryCommitted() const noexcept
    {
        return m_state ? m_state->MemoryCommitted() : 0;
    }
    UIntSize Document::NodeCount() const noexcept { return m_state ? m_state->nodes.size() : 0; }
    UIntSize Document::MemberCount() const noexcept { return m_state ? m_state->members.size() : 0; }

}// namespace NGIN::Serialization::JSON

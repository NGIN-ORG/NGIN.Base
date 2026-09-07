#pragma once

#include "../NameIndex.hpp"
#include "../SourceBuffer.hpp"
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/Core/ParseScratch.hpp>
#include <NGIN/Serialization/Core/SegmentedArena.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>
#include <NGIN/Serialization/JSON/JsonTypes.hpp>

#include <optional>
#include <string_view>
#include <vector>


namespace NGIN::Serialization::JSON::detail
{
    using NGIN::Serialization::detail::AllocationBudget;
    using NGIN::Serialization::detail::BudgetAllocator;
    using NGIN::Serialization::detail::BudgetVector;
    using NGIN::Serialization::detail::IndexedNames;
    using NGIN::Serialization::detail::NameIndex;
    struct StoredSpan
    {
        UIntSize begin {0};
        UIntSize end {0};
        StoredSpan() = default;
        StoredSpan(SourceSpan span) noexcept : begin(span.begin), end(span.end) {}
    };
    struct StringRef
    {
        const char* data {nullptr};
        UIntSize    size {0};

        [[nodiscard]] std::string_view View() const noexcept
        {
            return {data, size};
        }
    };

    struct NodeRange
    {
        UIntSize begin {0};
        UIntSize count {0};
    };

    union NodePayload
    {
        constexpr NodePayload() noexcept
            : unsignedValue(0)
        {
        }

        bool      boolValue;
        Int64     signedValue;
        UInt64    unsignedValue;
        F64       doubleValue;
        StringRef stringValue;
        NodeRange rangeValue;
    };

    struct NodeRecord
    {
        ValueKind   kind {ValueKind::Null};
        UInt32      nextSibling {NameIndex::Missing};
        StoredSpan  span {};
        NodePayload payload {};
    };

    struct MemberRecord
    {
        StringRef  key {};
        NodeId     value {};
        StoredSpan span {};
    };

    struct DocumentState
    {
        explicit DocumentState(OwnedTextBuffer       input,
                               const ParseLimits&    parseLimits,
                               const ParseResources& resources = {})
            : ownedSource(std::move(input)),
              source(ownedSource->View()),
              sourceId(ownedSource->Source()),
              limits(parseLimits),
              budget(resources.allocator, source.size() <= limits.maxTotalMemoryBytes ? limits.maxTotalMemoryBytes - source.size() : 0),
              arena(Memory::PolyAllocatorRef {budget}, budget.MaxSize(), resources.initialArenaBlockBytes),
              nodes(BudgetAllocator<NodeRecord> {budget}),
              elements(BudgetAllocator<NodeId> {budget}),
              members(BudgetAllocator<MemberRecord> {budget}),
              indexes(BudgetAllocator<IndexedNames> {budget})
        {
        }

        explicit DocumentState(BorrowedTextView      input,
                               const ParseLimits&    parseLimits,
                               const ParseResources& resources = {})
            : source(input.View()),
              sourceId(input.Source()),
              limits(parseLimits),
              budget(resources.allocator, limits.maxTotalMemoryBytes),
              arena(Memory::PolyAllocatorRef {budget}, budget.MaxSize(), resources.initialArenaBlockBytes),
              nodes(BudgetAllocator<NodeRecord> {budget}),
              elements(BudgetAllocator<NodeId> {budget}),
              members(BudgetAllocator<MemberRecord> {budget}),
              indexes(BudgetAllocator<IndexedNames> {budget})
        {
        }

        [[nodiscard]] const NodeRecord* Node(NodeId id) const noexcept
        {
            return id.IsValid() && id.value < nodes.size() ? &nodes[id.value] : nullptr;
        }

        [[nodiscard]] UIntSize MemoryUsed() const noexcept
        {
            return (ownedSource ? source.size() : 0) +
                   nodes.size() * sizeof(NodeRecord) +
                   elements.size() * sizeof(NodeId) +
                   members.size() * sizeof(MemberRecord) +
                   IndexMemoryUsed() +
                   arena.UsedBytes();
        }

        [[nodiscard]] UIntSize MemoryCommitted() const noexcept
        {
            return (ownedSource ? source.size() : 0) + budget.CommittedBytes();
        }

        [[nodiscard]] bool WithinMemoryLimit() const noexcept
        {
            return MemoryCommitted() <= limits.maxTotalMemoryBytes;
        }

        [[nodiscard]] SourceSpan ExpandSpan(StoredSpan span) const noexcept { return {sourceId, span.begin, span.end}; }
        [[nodiscard]] UIntSize   IndexMemoryUsed() const noexcept
        {
            UIntSize bytes = indexes.size() * sizeof(IndexedNames);
            for (const auto& entry: indexes)
                bytes += entry.index.MemoryUsed();
            return bytes;
        }
        [[nodiscard]] UInt32 FindMember(UIntSize begin, UIntSize count, std::string_view key) const noexcept;


        std::optional<OwnedTextBuffer> ownedSource {};
        std::string_view               source {};
        SourceId                       sourceId {};
        ParseLimits                    limits {};
        AllocationBudget               budget;
        SegmentedArena                 arena;
        BudgetVector<NodeRecord>       nodes;
        BudgetVector<NodeId>           elements;
        BudgetVector<MemberRecord>     members;
        BudgetVector<IndexedNames>     indexes;
        NodeId                         root {};
    };

    struct DocumentAccess
    {
        [[nodiscard]] static Document MakeDocument(std::unique_ptr<DocumentState> state) noexcept
        {
            return Document {std::move(state)};
        }

    };
    // Temporary document for synchronous event validation/emission only.
    [[nodiscard]] NGIN::Utilities::Expected<Document, ParseDiagnostic>
    ParseDocumentView(std::string_view input, ParseScratch& scratch,
                      const ParseOptions& options, const ParseLimits& limits);
}// namespace NGIN::Serialization::JSON::detail

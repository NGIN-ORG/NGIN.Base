#pragma once

#include "../NameIndex.hpp"
#include "../SourceBuffer.hpp"
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/Core/ParseScratch.hpp>
#include <NGIN/Serialization/Core/SegmentedArena.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <type_traits>
#include <vector>


namespace NGIN::Serialization::XML::detail
{
    using NGIN::Serialization::detail::AllocationBudget;
    using NGIN::Serialization::detail::BudgetAllocator;
    using NGIN::Serialization::detail::BudgetVector;
    using NGIN::Serialization::detail::IndexedNames;
    using NGIN::Serialization::detail::NameIndex;

    struct CompactSpan
    {
        UInt32 begin {0};
        UInt32 end {0};
    };

    struct TextRef
    {
        static constexpr UInt32 DecodedBit = UInt32 {1} << 31;

        UInt32 offsetOrId {0};
        UInt32 length {0};

        [[nodiscard]] bool IsDecoded() const noexcept
        {
            return (offsetOrId & DecodedBit) != 0;
        }

        [[nodiscard]] UInt32 OffsetOrId() const noexcept
        {
            return offsetOrId & ~DecodedBit;
        }
    };

    struct TableRange
    {
        UInt32 begin {0};
        UInt32 count {0};
    };

    struct SiblingRange
    {
        UInt32 first {(std::numeric_limits<UInt32>::max)()};
        UInt32 count {0};
    };

    struct NodeRecord
    {
        NodeKind kind {NodeKind::Text};
        UInt8    reserved0 {0};
        UInt16   reserved1 {0};
        UInt32   nextSibling {(std::numeric_limits<UInt32>::max)()};
        // Element nodes store their ElementRecord index in name.offsetOrId.
        TextRef     name {};
        TextRef     text {};
        CompactSpan span {};
    };

    struct AttributeRecord
    {
        TextRef     name {};
        TextRef     value {};
        CompactSpan span {};
        CompactSpan valueSpan {};
    };

    struct ElementRecord
    {
        TextRef      name {};
        TableRange   attributes {};
        SiblingRange children {};
        CompactSpan  span {};
    };

    static_assert(sizeof(NodeRecord) <= 32);
    static_assert(sizeof(AttributeRecord) <= 40);
    static_assert(sizeof(ElementRecord) <= 32);
    static_assert(sizeof(NodeView) <= 16);
    static_assert(sizeof(AttributeView) <= 16);
    static_assert(sizeof(ElementView) <= 16);
    static_assert(sizeof(AttributeRange) <= 16);
    static_assert(sizeof(ChildRange) <= 16);

    struct DocumentState
    {
        explicit DocumentState(OwnedTextBuffer       input,
                               const ParseLimits&    parseLimits,
                               const ParseResources& resources = {})
            : ownedSource(std::move(input)),
              source(ownedSource->View()),
              sourceId(ownedSource->Source()),
              limits(parseLimits),
              budget(resources.allocator, StorageBudget(parseLimits, source.size())),
              arena(NGIN::Memory::PolyAllocatorRef {budget},
                    budget.MaxSize(),
                    resources.initialArenaBlockBytes),
              nodes(BudgetAllocator<NodeRecord> {budget}),
              elements(BudgetAllocator<ElementRecord> {budget}),
              attributes(BudgetAllocator<AttributeRecord> {budget}),
              decodedTextPointers(BudgetAllocator<const char*> {budget}),
              attributeIndexes(BudgetAllocator<IndexedNames> {budget})
        {
        }

        explicit DocumentState(BorrowedTextView      input,
                               const ParseLimits&    parseLimits,
                               const ParseResources& resources = {})
            : source(input.View()),
              sourceId(input.Source()),
              limits(parseLimits),
              budget(resources.allocator, parseLimits.maxTotalMemoryBytes),
              arena(NGIN::Memory::PolyAllocatorRef {budget},
                    budget.MaxSize(),
                    resources.initialArenaBlockBytes),
              nodes(BudgetAllocator<NodeRecord> {budget}),
              elements(BudgetAllocator<ElementRecord> {budget}),
              attributes(BudgetAllocator<AttributeRecord> {budget}),
              decodedTextPointers(BudgetAllocator<const char*> {budget}),
              attributeIndexes(BudgetAllocator<IndexedNames> {budget})
        {
        }

        [[nodiscard]] static UIntSize StorageBudget(const ParseLimits& limits,
                                                    UIntSize           ownedSourceBytes) noexcept
        {
            return ownedSourceBytes <= limits.maxTotalMemoryBytes
                           ? limits.maxTotalMemoryBytes - ownedSourceBytes
                           : 0;
        }

        template<class T>
        [[nodiscard]] BudgetAllocator<T> Allocator() noexcept
        {
            return BudgetAllocator<T> {budget};
        }

        [[nodiscard]] const NodeRecord* Node(NodeId id) const noexcept
        {
            return id.IsValid() && id.value < nodes.size() ? &nodes[id.value] : nullptr;
        }

        [[nodiscard]] NodeRecord* Node(NodeId id) noexcept
        {
            return id.IsValid() && id.value < nodes.size() ? &nodes[id.value] : nullptr;
        }

        [[nodiscard]] CompactSpan MakeSpan(UIntSize begin, UIntSize end) const noexcept
        {
            return {
                    static_cast<UInt32>(begin),
                    static_cast<UInt32>(end),
            };
        }

        [[nodiscard]] SourceSpan ExpandSpan(CompactSpan span) const noexcept
        {
            return {sourceId, span.begin, span.end};
        }

        [[nodiscard]] TextRef StoreText(std::string_view value)
        {
            if (value.empty())
                return {};
            if (value.size() > (std::numeric_limits<UInt32>::max)())
                throw std::bad_alloc {};

            const auto sourceAddress = reinterpret_cast<std::uintptr_t>(source.data());
            const auto valueAddress  = reinterpret_cast<std::uintptr_t>(value.data());
            if (valueAddress >= sourceAddress &&
                valueAddress - sourceAddress <= source.size() &&
                value.size() <= source.size() - (valueAddress - sourceAddress))
            {
                return {
                        static_cast<UInt32>(valueAddress - sourceAddress),
                        static_cast<UInt32>(value.size()),
                };
            }

            if (decodedTextPointers.size() >= TextRef::DecodedBit)
                throw std::bad_alloc {};
            const auto id = static_cast<UInt32>(decodedTextPointers.size());
            decodedTextPointers.push_back(value.data());
            return {
                    id | TextRef::DecodedBit,
                    static_cast<UInt32>(value.size()),
            };
        }

        [[nodiscard]] TextRef SourceText(UIntSize offset, UIntSize length) const noexcept
        {
            return {
                    static_cast<UInt32>(offset),
                    static_cast<UInt32>(length),
            };
        }

        [[nodiscard]] std::string_view Text(TextRef reference) const noexcept
        {
            if (reference.length == 0)
                return {};
            if (reference.IsDecoded())
            {
                const auto id = reference.OffsetOrId();
                return id < decodedTextPointers.size()
                               ? std::string_view {decodedTextPointers[id], reference.length}
                               : std::string_view {};
            }
            const auto offset = reference.OffsetOrId();
            return offset <= source.size() && reference.length <= source.size() - offset
                           ? source.substr(offset, reference.length)
                           : std::string_view {};
        }

        [[nodiscard]] SourceSpan TextSpan(TextRef reference) const noexcept
        {
            return reference.IsDecoded()
                           ? SourceSpan {}
                           : SourceSpan {
                                     sourceId,
                                     reference.OffsetOrId(),
                                     static_cast<UIntSize>(reference.OffsetOrId()) + reference.length,
                             };
        }

        [[nodiscard]] UIntSize MemoryUsed() const noexcept
        {
            const UIntSize ownedBytes = ownedSource ? source.size() : 0;
            return ownedBytes +
                   nodes.size() * sizeof(NodeRecord) +
                   elements.size() * sizeof(ElementRecord) +
                   attributes.size() * sizeof(AttributeRecord) +
                   decodedTextPointers.size() * sizeof(const char*) +
                   IndexMemoryUsed() + arena.UsedBytes();
        }

        [[nodiscard]] UIntSize MemoryCommitted() const noexcept
        {
            const UIntSize ownedBytes = ownedSource ? source.size() : 0;
            return ownedBytes + budget.CommittedBytes();
        }

        [[nodiscard]] UIntSize PeakMemoryCommitted() const noexcept
        {
            const UIntSize ownedBytes = ownedSource ? source.size() : 0;
            return ownedBytes + budget.PeakCommittedBytes();
        }

        [[nodiscard]] bool WithinMemoryLimit() const noexcept
        {
            return MemoryCommitted() <= limits.maxTotalMemoryBytes;
        }

        [[nodiscard]] UIntSize IndexMemoryUsed() const noexcept
        {
            UIntSize bytes = attributeIndexes.size() * sizeof(IndexedNames);
            for (const auto& entry: attributeIndexes)
                bytes += entry.index.MemoryUsed();
            return bytes;
        }
        [[nodiscard]] UInt32 FindAttribute(TableRange range, std::string_view name) const noexcept;

        std::optional<OwnedTextBuffer> ownedSource {};
        std::string_view               source {};
        SourceId                       sourceId {};
        ParseLimits                    limits {};
        AllocationBudget               budget;
        SegmentedArena                 arena;
        BudgetVector<NodeRecord>       nodes;
        BudgetVector<ElementRecord>    elements;
        BudgetVector<AttributeRecord>  attributes;
        BudgetVector<const char*>      decodedTextPointers;
        BudgetVector<IndexedNames>     attributeIndexes;
        UIntSize                       childCount {0};
        NodeId                         root {};
    };

    struct SyntaxState
    {
        OwnedTextBuffer          source;
        std::vector<SyntaxToken> tokens;
        bool                     valid {false};
    };

    struct DocumentAccess
    {
        [[nodiscard]] static Document MakeDocument(std::unique_ptr<DocumentState> state) noexcept
        {
            return Document {std::move(state)};
        }

    };
}// namespace NGIN::Serialization::XML::detail

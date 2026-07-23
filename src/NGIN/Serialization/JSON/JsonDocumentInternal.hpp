#pragma once

#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/Core/SegmentedArena.hpp>
#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/JSON/JsonTypes.hpp>

#include <optional>
#include <string_view>
#include <vector>


namespace NGIN::Serialization::JSON::detail
{
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
        SourceSpan  span {};
        NodePayload payload {};
    };

    struct MemberRecord
    {
        StringRef  key {};
        NodeId     value {};
        SourceSpan span {};
    };

    struct DocumentState
    {
        explicit DocumentState(OwnedTextBuffer       input,
                               const ParseLimits&    parseLimits,
                               const ParseResources& resources = {})
            : ownedSource(std::move(input)),
              source(ownedSource->View()),
              sourceId(ownedSource->Source()),
              arena(resources.allocator,
                    parseLimits.maxTotalMemoryBytes,
                    resources.initialArenaBlockBytes),
              limits(parseLimits)
        {
        }

        explicit DocumentState(BorrowedTextView      input,
                               const ParseLimits&    parseLimits,
                               const ParseResources& resources = {})
            : source(input.View()),
              sourceId(input.Source()),
              arena(resources.allocator,
                    parseLimits.maxTotalMemoryBytes,
                    resources.initialArenaBlockBytes),
              limits(parseLimits)
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
                   valueViews.size() * sizeof(ValueView) +
                   arena.UsedBytes();
        }

        [[nodiscard]] UIntSize MemoryCommitted() const noexcept
        {
            return (ownedSource ? source.size() : 0) +
                   nodes.capacity() * sizeof(NodeRecord) +
                   elements.capacity() * sizeof(NodeId) +
                   members.capacity() * sizeof(MemberRecord) +
                   valueViews.capacity() * sizeof(ValueView) +
                   arena.CommittedBytes();
        }

        [[nodiscard]] bool WithinMemoryLimit() const noexcept
        {
            return MemoryCommitted() <= limits.maxTotalMemoryBytes;
        }

        void FinalizeViews();

        std::optional<OwnedTextBuffer> ownedSource {};
        std::string_view               source {};
        SourceId                       sourceId {};
        SegmentedArena                 arena;
        ParseLimits                    limits {};
        std::vector<NodeRecord>        nodes {};
        std::vector<NodeId>            elements {};
        std::vector<MemberRecord>      members {};
        std::vector<ValueView>         valueViews {};
        NodeId                         root {};
    };

    struct DocumentAccess
    {
        [[nodiscard]] static Document MakeDocument(std::unique_ptr<DocumentState> state) noexcept
        {
            return Document {std::move(state)};
        }

        [[nodiscard]] static BorrowedDocument MakeBorrowedDocument(std::unique_ptr<DocumentState> state) noexcept
        {
            return BorrowedDocument {std::move(state)};
        }
    };
}// namespace NGIN::Serialization::JSON::detail

#pragma once

#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/Core/SegmentedArena.hpp>
#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>

#include <optional>
#include <vector>


namespace NGIN::Serialization::XML::detail
{
    struct StringRef
    {
        const char* data {nullptr};
        UIntSize    size {0};

        [[nodiscard]] std::string_view View() const noexcept { return {data, size}; }
    };

    struct Range
    {
        UIntSize begin {0};
        UIntSize count {0};
    };

    struct NodeRecord
    {
        NodeKind   kind {NodeKind::Text};
        SourceSpan span {};
        UIntSize   element {0};
        StringRef  name {};
        StringRef  text {};
    };

    struct AttributeRecord
    {
        StringRef  name {};
        StringRef  value {};
        SourceSpan span {};
        SourceSpan nameSpan {};
        SourceSpan valueSpan {};
    };

    struct ElementRecord
    {
        StringRef  name {};
        Range      attributes {};
        Range      children {};
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
            const UIntSize ownedBytes = ownedSource ? source.size() : 0;
            return ownedBytes +
                   nodes.size() * sizeof(NodeRecord) +
                   elements.size() * sizeof(ElementRecord) +
                   attributes.size() * sizeof(AttributeRecord) +
                   children.size() * sizeof(NodeId) +
                   elementViews.size() * sizeof(ElementView) +
                   arena.UsedBytes();
        }

        [[nodiscard]] UIntSize MemoryCommitted() const noexcept
        {
            const UIntSize ownedBytes = ownedSource ? source.size() : 0;
            return ownedBytes +
                   nodes.capacity() * sizeof(NodeRecord) +
                   elements.capacity() * sizeof(ElementRecord) +
                   attributes.capacity() * sizeof(AttributeRecord) +
                   children.capacity() * sizeof(NodeId) +
                   elementViews.capacity() * sizeof(ElementView) +
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
        std::vector<ElementRecord>     elements {};
        std::vector<AttributeRecord>   attributes {};
        std::vector<NodeId>            children {};
        std::vector<ElementView>       elementViews {};
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

        [[nodiscard]] static BorrowedDocument MakeBorrowedDocument(std::unique_ptr<DocumentState> state) noexcept
        {
            return BorrowedDocument {std::move(state)};
        }
    };
}// namespace NGIN::Serialization::XML::detail

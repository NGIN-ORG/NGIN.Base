#pragma once

#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/Core/SegmentedArena.hpp>
#include <NGIN/Serialization/Core/SourceBuffer.hpp>
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
    class AllocationBudget
    {
    public:
        explicit AllocationBudget(NGIN::Memory::PolyAllocatorRef upstream,
                                  UIntSize                       maxCommittedBytes) noexcept
            : m_upstream(upstream ? upstream
                                  : NGIN::Memory::PolyAllocatorRef {m_systemAllocator}),
              m_maxCommittedBytes(maxCommittedBytes)
        {
        }

        [[nodiscard]] void* Allocate(UIntSize size, UIntSize alignment) noexcept
        {
            if (size == 0)
                return nullptr;
            if (size > Remaining())
            {
                m_limitExceeded = true;
                return nullptr;
            }
            void* memory = m_upstream.Allocate(size, alignment);
            if (!memory)
                return nullptr;
            m_committedBytes += size;
            m_peakCommittedBytes = (std::max) (m_peakCommittedBytes, m_committedBytes);
            ++m_allocationCount;
            return memory;
        }

        void Deallocate(void* memory, UIntSize size, UIntSize alignment) noexcept
        {
            if (!memory)
                return;
            m_upstream.Deallocate(memory, size, alignment);
            m_committedBytes = size <= m_committedBytes ? m_committedBytes - size : 0;
        }

        [[nodiscard]] UIntSize MaxSize() const noexcept { return m_maxCommittedBytes; }
        [[nodiscard]] UIntSize Remaining() const noexcept
        {
            return m_committedBytes <= m_maxCommittedBytes
                           ? m_maxCommittedBytes - m_committedBytes
                           : 0;
        }
        [[nodiscard]] UIntSize CommittedBytes() const noexcept { return m_committedBytes; }
        [[nodiscard]] UIntSize PeakCommittedBytes() const noexcept { return m_peakCommittedBytes; }
        [[nodiscard]] UIntSize AllocationCount() const noexcept { return m_allocationCount; }
        [[nodiscard]] bool     LimitExceeded() const noexcept { return m_limitExceeded; }

    private:
        NGIN::Memory::SystemAllocator  m_systemAllocator {};
        NGIN::Memory::PolyAllocatorRef m_upstream {};
        UIntSize                       m_maxCommittedBytes {0};
        UIntSize                       m_committedBytes {0};
        UIntSize                       m_peakCommittedBytes {0};
        UIntSize                       m_allocationCount {0};
        bool                           m_limitExceeded {false};
    };

    template<class T>
    class BudgetAllocator
    {
    public:
        using value_type = T;

        BudgetAllocator() noexcept = default;
        explicit BudgetAllocator(AllocationBudget& budget) noexcept
            : m_budget(&budget)
        {
        }

        template<class U>
        BudgetAllocator(const BudgetAllocator<U>& other) noexcept
            : m_budget(other.Budget())
        {
        }

        [[nodiscard]] T* allocate(std::size_t count)
        {
            if (!m_budget || count > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
                throw std::bad_alloc {};
            void* memory = m_budget->Allocate(count * sizeof(T), alignof(T));
            if (!memory)
                throw std::bad_alloc {};
            return static_cast<T*>(memory);
        }

        void deallocate(T* memory, std::size_t count) noexcept
        {
            if (m_budget)
                m_budget->Deallocate(memory, count * sizeof(T), alignof(T));
        }

        [[nodiscard]] AllocationBudget* Budget() const noexcept { return m_budget; }

        template<class U>
        [[nodiscard]] bool operator==(const BudgetAllocator<U>& other) const noexcept
        {
            return m_budget == other.Budget();
        }

    private:
        AllocationBudget* m_budget {nullptr};
    };

    template<class T>
    using BudgetVector = std::vector<T, BudgetAllocator<T>>;

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
              decodedTextPointers(BudgetAllocator<const char*> {budget})
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
              decodedTextPointers(BudgetAllocator<const char*> {budget})
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
                   arena.UsedBytes();
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

        [[nodiscard]] static BorrowedDocument MakeBorrowedDocument(std::unique_ptr<DocumentState> state) noexcept
        {
            return BorrowedDocument {std::move(state)};
        }
    };
}// namespace NGIN::Serialization::XML::detail

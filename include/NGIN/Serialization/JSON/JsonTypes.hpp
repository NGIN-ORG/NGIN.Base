#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Core/SourceSpan.hpp>

#include <iterator>
#include <memory>
#include <optional>
#include <string_view>

namespace NGIN::Serialization::JSON
{
    namespace detail
    {
        struct DocumentState;
        struct DocumentAccess;
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

    enum class ValueKind : UInt8
    {
        Null,
        Bool,
        Int64,
        UInt64,
        Double,
        String,
        Array,
        Object,
    };

    class ArrayView;
    class ObjectView;
    class MemberView;

    /// @brief Immutable, checked view of one JSON value.
    class NGIN_SERIALIZATION_API ValueView
    {
    public:
        enum class Type : UInt8
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object,
        };

        constexpr ValueView() noexcept = default;

        [[nodiscard]] bool       IsValid() const noexcept;
        [[nodiscard]] ValueKind  Kind() const noexcept;
        [[nodiscard]] SourceSpan Span() const noexcept;

        [[nodiscard]] bool IsNull() const noexcept;
        [[nodiscard]] bool IsBool() const noexcept;
        [[nodiscard]] bool IsInt64() const noexcept;
        [[nodiscard]] bool IsUInt64() const noexcept;
        [[nodiscard]] bool IsDouble() const noexcept;
        [[nodiscard]] bool IsNumber() const noexcept;
        [[nodiscard]] bool IsString() const noexcept;
        [[nodiscard]] bool IsArray() const noexcept;
        [[nodiscard]] bool IsObject() const noexcept;

        [[nodiscard]] std::optional<bool>             TryBool() const noexcept;
        [[nodiscard]] std::optional<Int64>            TryInt64() const noexcept;
        [[nodiscard]] std::optional<UInt64>           TryUInt64() const noexcept;
        [[nodiscard]] std::optional<F64>              TryDouble() const noexcept;
        [[nodiscard]] std::optional<std::string_view> TryString() const noexcept;
        [[nodiscard]] std::optional<ArrayView>        TryArray() const noexcept;
        [[nodiscard]] std::optional<ObjectView>       TryObject() const noexcept;

        [[nodiscard]] Type             GetType() const noexcept;
        [[nodiscard]] bool             AsBool() const noexcept;
        [[nodiscard]] F64              AsNumber() const noexcept;
        [[nodiscard]] std::string_view AsString() const noexcept;
        [[nodiscard]] ArrayView        AsArray() const noexcept;
        [[nodiscard]] ObjectView       AsObject() const noexcept;

        [[nodiscard]] NodeId Id() const noexcept { return m_id; }

    private:
        friend class ArrayView;
        friend class ObjectView;
        friend class MemberView;
        friend class Document;
        friend class BorrowedDocument;
        friend class Builder;
        friend struct detail::DocumentState;

        constexpr ValueView(const detail::DocumentState* state, NodeId id) noexcept
            : m_state(state), m_id(id)
        {
        }

        const detail::DocumentState* m_state {nullptr};
        NodeId                       m_id {};
    };

    /// @brief Immutable view of one JSON object member.
    class NGIN_SERIALIZATION_API MemberView
    {
    public:
        constexpr MemberView() noexcept = default;

        [[nodiscard]] bool             IsValid() const noexcept;
        [[nodiscard]] std::string_view Key() const noexcept;
        [[nodiscard]] ValueView        Value() const noexcept;
        [[nodiscard]] SourceSpan       Span() const noexcept;

    private:
        friend class ObjectView;

        constexpr MemberView(const detail::DocumentState* state, UIntSize index) noexcept
            : m_state(state), m_index(index)
        {
        }

        const detail::DocumentState* m_state {nullptr};
        UIntSize                     m_index {0};
    };

    /// @brief Immutable contiguous view of a JSON array.
    class NGIN_SERIALIZATION_API ArrayView
    {
    public:
        class NGIN_SERIALIZATION_API Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = ValueView;
            using difference_type   = std::ptrdiff_t;

            constexpr Iterator() noexcept = default;

            [[nodiscard]] ValueView   operator*() const noexcept;
            Iterator&                 operator++() noexcept;
            Iterator                  operator++(int) noexcept;
            [[nodiscard]] friend bool operator==(const Iterator&, const Iterator&) noexcept = default;

        private:
            friend class ArrayView;

            constexpr Iterator(const detail::DocumentState* state, UIntSize index) noexcept
                : m_state(state), m_index(index)
            {
            }

            const detail::DocumentState* m_state {nullptr};
            UIntSize                     m_index {0};
        };

        constexpr ArrayView() noexcept = default;

        [[nodiscard]] bool       IsValid() const noexcept;
        [[nodiscard]] UIntSize   Size() const noexcept;
        [[nodiscard]] bool       Empty() const noexcept { return Size() == 0; }
        [[nodiscard]] ValueView  operator[](UIntSize index) const noexcept;
        [[nodiscard]] Iterator   begin() const noexcept;
        [[nodiscard]] Iterator   end() const noexcept;
        [[nodiscard]] SourceSpan Span() const noexcept;

    private:
        friend class ValueView;
        friend class Builder;

        constexpr ArrayView(const detail::DocumentState* state, UIntSize begin, UIntSize count, SourceSpan span) noexcept
            : m_state(state), m_begin(begin), m_count(count), m_span(span)
        {
        }

        const detail::DocumentState* m_state {nullptr};
        UIntSize                     m_begin {0};
        UIntSize                     m_count {0};
        SourceSpan                   m_span {};
    };

    /// @brief Immutable contiguous view of a JSON object.
    class NGIN_SERIALIZATION_API ObjectView
    {
    public:
        class NGIN_SERIALIZATION_API Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = MemberView;
            using difference_type   = std::ptrdiff_t;

            constexpr Iterator() noexcept = default;

            [[nodiscard]] MemberView  operator*() const noexcept;
            Iterator&                 operator++() noexcept;
            Iterator                  operator++(int) noexcept;
            [[nodiscard]] friend bool operator==(const Iterator&, const Iterator&) noexcept = default;

        private:
            friend class ObjectView;

            constexpr Iterator(const detail::DocumentState* state, UIntSize index) noexcept
                : m_state(state), m_index(index)
            {
            }

            const detail::DocumentState* m_state {nullptr};
            UIntSize                     m_index {0};
        };

        constexpr ObjectView() noexcept = default;

        [[nodiscard]] bool                     IsValid() const noexcept;
        [[nodiscard]] UIntSize                 Size() const noexcept;
        [[nodiscard]] bool                     Empty() const noexcept { return Size() == 0; }
        [[nodiscard]] MemberView               MemberAt(UIntSize index) const noexcept;
        [[nodiscard]] std::optional<ValueView> Find(std::string_view key) const noexcept;
        [[nodiscard]] const ValueView*         FindPtr(std::string_view key) const noexcept;
        [[nodiscard]] Iterator                 begin() const noexcept;
        [[nodiscard]] Iterator                 end() const noexcept;
        [[nodiscard]] SourceSpan               Span() const noexcept;

    private:
        friend class ValueView;
        friend class Builder;

        constexpr ObjectView(const detail::DocumentState* state, UIntSize begin, UIntSize count, SourceSpan span) noexcept
            : m_state(state), m_begin(begin), m_count(count), m_span(span)
        {
        }

        const detail::DocumentState* m_state {nullptr};
        UIntSize                     m_begin {0};
        UIntSize                     m_count {0};
        SourceSpan                   m_span {};
    };

    /// @brief Self-contained owning JSON document.
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
        [[nodiscard]] ValueView        Root() const noexcept;
        [[nodiscard]] std::string_view SourceText() const noexcept;
        [[nodiscard]] UIntSize         MemoryUsed() const noexcept;
        [[nodiscard]] UIntSize         MemoryCommitted() const noexcept;
        [[nodiscard]] UIntSize         NodeCount() const noexcept;
        [[nodiscard]] UIntSize         MemberCount() const noexcept;

    private:
        friend class Parser;
        friend class Builder;
        friend struct detail::DocumentAccess;

        explicit Document(std::unique_ptr<detail::DocumentState> state) noexcept;

        std::unique_ptr<detail::DocumentState> m_state;
    };

    /// @brief Explicitly non-owning JSON document tied to a caller-owned source.
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
        [[nodiscard]] ValueView        Root() const noexcept;
        [[nodiscard]] std::string_view SourceText() const noexcept;
        [[nodiscard]] UIntSize         MemoryUsed() const noexcept;
        [[nodiscard]] UIntSize         MemoryCommitted() const noexcept;
        [[nodiscard]] UIntSize         NodeCount() const noexcept;
        [[nodiscard]] UIntSize         MemberCount() const noexcept;

    private:
        friend class Parser;
        friend struct detail::DocumentAccess;

        explicit BorrowedDocument(std::unique_ptr<detail::DocumentState> state) noexcept;

        std::unique_ptr<detail::DocumentState> m_state;
    };
}// namespace NGIN::Serialization::JSON

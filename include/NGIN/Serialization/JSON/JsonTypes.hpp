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

    /// @brief Stable index identifying a value inside one JSON document state.
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

    /// @brief Exact storage kind of a JSON value.
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

        /// @brief Constructs an invalid value view.
        constexpr ValueView() noexcept = default;

        /// @brief Returns whether this view refers to a live document node.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the exact storage kind, or `Null` for an invalid view.
        [[nodiscard]] ValueKind Kind() const noexcept;
        /// @brief Returns the source range that produced this value.
        [[nodiscard]] SourceSpan Span() const noexcept;

        /// @brief Returns whether this value is JSON null.
        [[nodiscard]] bool IsNull() const noexcept;
        /// @brief Returns whether this value is Boolean.
        [[nodiscard]] bool IsBool() const noexcept;
        /// @brief Returns whether this value is stored as a signed integer.
        [[nodiscard]] bool IsInt64() const noexcept;
        /// @brief Returns whether this value is stored as an unsigned integer.
        [[nodiscard]] bool IsUInt64() const noexcept;
        /// @brief Returns whether this value is stored as floating point.
        [[nodiscard]] bool IsDouble() const noexcept;
        /// @brief Returns whether this value is any numeric storage kind.
        [[nodiscard]] bool IsNumber() const noexcept;
        /// @brief Returns whether this value is a string.
        [[nodiscard]] bool IsString() const noexcept;
        /// @brief Returns whether this value is an array.
        [[nodiscard]] bool IsArray() const noexcept;
        /// @brief Returns whether this value is an object.
        [[nodiscard]] bool IsObject() const noexcept;

        /// @brief Returns the Boolean value when its kind matches.
        [[nodiscard]] std::optional<bool> TryBool() const noexcept;
        /// @brief Returns the value as a signed integer when exactly representable.
        [[nodiscard]] std::optional<Int64> TryInt64() const noexcept;
        /// @brief Returns the value as an unsigned integer when exactly representable.
        [[nodiscard]] std::optional<UInt64> TryUInt64() const noexcept;
        /// @brief Returns the value converted to floating point when numeric.
        [[nodiscard]] std::optional<F64> TryDouble() const noexcept;
        /// @brief Returns a borrowed decoded string when the value is a string.
        [[nodiscard]] std::optional<std::string_view> TryString() const noexcept;
        /// @brief Returns an array view when the value is an array.
        [[nodiscard]] std::optional<ArrayView> TryArray() const noexcept;
        /// @brief Returns an object view when the value is an object.
        [[nodiscard]] std::optional<ObjectView> TryObject() const noexcept;

        /// @brief Returns the broad compatibility type used by legacy-style checked accessors.
        [[nodiscard]] Type GetType() const noexcept;
        /// @brief Returns the Boolean value.
        /// @pre IsBool() is true; a failed check aborts in assertion-enabled builds.
        [[nodiscard]] bool AsBool() const noexcept;
        /// @brief Returns any numeric value converted to floating point.
        /// @pre IsNumber() is true; a failed check aborts in assertion-enabled builds.
        [[nodiscard]] F64 AsNumber() const noexcept;
        /// @brief Returns a borrowed decoded string.
        /// @pre IsString() is true; the view remains valid only while the document lives.
        [[nodiscard]] std::string_view AsString() const noexcept;
        /// @brief Returns this value as an array view.
        /// @pre IsArray() is true.
        [[nodiscard]] ArrayView AsArray() const noexcept;
        /// @brief Returns this value as an object view.
        /// @pre IsObject() is true.
        [[nodiscard]] ObjectView AsObject() const noexcept;

        /// @brief Returns the document-local node identifier.
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
        /// @brief Constructs an invalid member view.
        constexpr MemberView() noexcept = default;

        /// @brief Returns whether this view refers to a live object member.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the borrowed decoded member key.
        [[nodiscard]] std::string_view Key() const noexcept;
        /// @brief Returns the member value view.
        [[nodiscard]] ValueView Value() const noexcept;
        /// @brief Returns the source range covering the member.
        [[nodiscard]] SourceSpan Span() const noexcept;

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

            /// @brief Constructs an invalid iterator.
            constexpr Iterator() noexcept = default;

            /// @brief Returns the value at the current array position.
            [[nodiscard]] ValueView operator*() const noexcept;
            /// @brief Advances to the next array value.
            Iterator& operator++() noexcept;
            /// @brief Advances and returns the previous iterator value.
            Iterator operator++(int) noexcept;
            /// @brief Compares iterator document and position state.
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

        /// @brief Constructs an invalid array view.
        constexpr ArrayView() noexcept = default;

        /// @brief Returns whether this view refers to a live JSON array.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the number of array values.
        [[nodiscard]] UIntSize Size() const noexcept;
        /// @brief Returns whether the array contains no values.
        [[nodiscard]] bool Empty() const noexcept { return Size() == 0; }
        /// @brief Returns the value at @p index.
        /// @pre @p index is less than Size().
        [[nodiscard]] ValueView operator[](UIntSize index) const noexcept;
        /// @brief Returns an iterator to the first value.
        [[nodiscard]] Iterator begin() const noexcept;
        /// @brief Returns the past-the-end iterator.
        [[nodiscard]] Iterator end() const noexcept;
        /// @brief Returns the source range covering the array.
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

            /// @brief Constructs an invalid iterator.
            constexpr Iterator() noexcept = default;

            /// @brief Returns the member at the current object position.
            [[nodiscard]] MemberView operator*() const noexcept;
            /// @brief Advances to the next object member.
            Iterator& operator++() noexcept;
            /// @brief Advances and returns the previous iterator value.
            Iterator operator++(int) noexcept;
            /// @brief Compares iterator document and position state.
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

        /// @brief Constructs an invalid object view.
        constexpr ObjectView() noexcept = default;

        /// @brief Returns whether this view refers to a live JSON object.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the number of object members, including duplicate keys.
        [[nodiscard]] UIntSize Size() const noexcept;
        /// @brief Returns whether the object contains no members.
        [[nodiscard]] bool Empty() const noexcept { return Size() == 0; }
        /// @brief Returns the member at @p index in source order.
        /// @pre @p index is less than Size().
        [[nodiscard]] MemberView MemberAt(UIntSize index) const noexcept;
        /// @brief Finds a member value by decoded key.
        [[nodiscard]] std::optional<ValueView> Find(std::string_view key) const noexcept;
        /// @brief Returns a pointer to a found value stored in document state, or null.
        /// @note The pointer is invalidated when the owning document is destroyed.
        [[nodiscard]] const ValueView* FindPtr(std::string_view key) const noexcept;
        /// @brief Returns an iterator to the first member.
        [[nodiscard]] Iterator begin() const noexcept;
        /// @brief Returns the past-the-end iterator.
        [[nodiscard]] Iterator end() const noexcept;
        /// @brief Returns the source range covering the object.
        [[nodiscard]] SourceSpan Span() const noexcept;

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
        /// @brief Constructs an empty document.
        Document() noexcept;
        /// @brief Releases all source text, nodes, and decoded storage.
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
        /// @brief Returns the root value view, valid while this document remains alive and unmoved-from.
        [[nodiscard]] ValueView Root() const noexcept;
        /// @brief Returns the owned source text used to create the document.
        [[nodiscard]] std::string_view SourceText() const noexcept;
        /// @brief Returns bytes currently used by document arenas.
        [[nodiscard]] UIntSize MemoryUsed() const noexcept;
        /// @brief Returns bytes committed by document arenas.
        [[nodiscard]] UIntSize MemoryCommitted() const noexcept;
        /// @brief Returns the number of stored JSON value nodes.
        [[nodiscard]] UIntSize NodeCount() const noexcept;
        /// @brief Returns the number of stored object members.
        [[nodiscard]] UIntSize MemberCount() const noexcept;

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
        /// @brief Constructs an empty borrowed document.
        BorrowedDocument() noexcept;
        /// @brief Releases parsed state without releasing caller-owned source storage.
        ~BorrowedDocument();

        /// @brief Transfers parsed state and its source borrowing relationship.
        BorrowedDocument(BorrowedDocument&&) noexcept;
        /// @brief Replaces this state with another borrowed document's state.
        BorrowedDocument& operator=(BorrowedDocument&&) noexcept;
        /// @brief Borrowed documents are non-copyable because their views refer directly to state.
        BorrowedDocument(const BorrowedDocument&) = delete;
        /// @brief Borrowed documents are non-copy-assignable because their views refer directly to state.
        BorrowedDocument& operator=(const BorrowedDocument&) = delete;

        /// @brief Returns whether the document contains parsed state.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns the root value view.
        /// @note The caller-owned source and this document must outlive every returned view.
        [[nodiscard]] ValueView Root() const noexcept;
        /// @brief Returns a view of the caller-owned source text.
        [[nodiscard]] std::string_view SourceText() const noexcept;
        /// @brief Returns bytes currently used by document arenas.
        [[nodiscard]] UIntSize MemoryUsed() const noexcept;
        /// @brief Returns bytes committed by document arenas.
        [[nodiscard]] UIntSize MemoryCommitted() const noexcept;
        /// @brief Returns the number of stored JSON value nodes.
        [[nodiscard]] UIntSize NodeCount() const noexcept;
        /// @brief Returns the number of stored object members.
        [[nodiscard]] UIntSize MemberCount() const noexcept;

    private:
        friend class Parser;
        friend struct detail::DocumentAccess;

        explicit BorrowedDocument(std::unique_ptr<detail::DocumentState> state) noexcept;

        std::unique_ptr<detail::DocumentState> m_state;
    };
}// namespace NGIN::Serialization::JSON

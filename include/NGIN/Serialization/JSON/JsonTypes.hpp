#pragma once

#include <NGIN/Containers/HashMap.hpp>
#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Memory/AllocatorRef.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Primitives.hpp>

#include <functional>
#include <cstring>
#include <string_view>

namespace NGIN::Serialization
{
    struct JsonArray;
    struct JsonObject;

    using JsonArena      = NGIN::Memory::LinearAllocator<>;
    using JsonAllocator  = NGIN::Memory::AllocatorRef<JsonArena>;
    using JsonStringView = std::string_view;

    /// @brief JSON value node with pointer-based arrays/objects.
    struct JsonValue
    {
        enum class Type : UInt8
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object,
        };

        constexpr JsonValue() noexcept
            : m_type(Type::Null), m_bool(false)
        {
        }

        static JsonValue MakeNull() noexcept
        {
            return JsonValue {};
        }

        static JsonValue MakeBool(bool value) noexcept
        {
            JsonValue v;
            v.m_type = Type::Bool;
            v.m_bool = value;
            return v;
        }

        static JsonValue MakeNumber(F64 value) noexcept
        {
            JsonValue v;
            v.m_type   = Type::Number;
            v.m_number = value;
            return v;
        }

        static JsonValue MakeString(JsonStringView value) noexcept
        {
            JsonValue v;
            v.m_type   = Type::String;
            v.m_string = value;
            return v;
        }

        static JsonValue MakeArray(JsonArray* value) noexcept
        {
            JsonValue v;
            v.m_type  = Type::Array;
            v.m_array = value;
            return v;
        }

        static JsonValue MakeObject(JsonObject* value) noexcept
        {
            JsonValue v;
            v.m_type   = Type::Object;
            v.m_object = value;
            return v;
        }

        [[nodiscard]] Type GetType() const noexcept { return m_type; }

        [[nodiscard]] bool IsNull() const noexcept { return m_type == Type::Null; }
        [[nodiscard]] bool IsBool() const noexcept { return m_type == Type::Bool; }
        [[nodiscard]] bool IsNumber() const noexcept { return m_type == Type::Number; }
        [[nodiscard]] bool IsString() const noexcept { return m_type == Type::String; }
        [[nodiscard]] bool IsArray() const noexcept { return m_type == Type::Array; }
        [[nodiscard]] bool IsObject() const noexcept { return m_type == Type::Object; }

        [[nodiscard]] bool              AsBool() const noexcept { return m_bool; }
        [[nodiscard]] F64               AsNumber() const noexcept { return m_number; }
        [[nodiscard]] JsonStringView    AsString() const noexcept { return m_string; }
        [[nodiscard]] JsonArray&        AsArray() noexcept { return *m_array; }
        [[nodiscard]] const JsonArray&  AsArray() const noexcept { return *m_array; }
        [[nodiscard]] JsonObject&       AsObject() noexcept { return *m_object; }
        [[nodiscard]] const JsonObject& AsObject() const noexcept { return *m_object; }

    private:
        Type m_type {Type::Null};
        union
        {
            bool           m_bool;
            F64            m_number;
            JsonStringView m_string;
            JsonArray*     m_array;
            JsonObject*    m_object;
        };
    };

    /// @brief Name/value member for JSON objects.
    struct JsonMember
    {
        JsonStringView name {};
        JsonValue      value {};
    };

    /// @brief JSON array container.
    struct JsonArray
    {
        explicit JsonArray(JsonAllocator allocator)
            : values(0, allocator)
        {
        }

        NGIN::Containers::Vector<JsonValue, JsonAllocator> values;
    };

    /// @brief JSON object container.
    struct JsonObject
    {
        explicit JsonObject(JsonAllocator allocator)
            : members(0, allocator), m_allocator(allocator)
        {
        }

        [[nodiscard]] JsonValue*       Find(JsonStringView key) noexcept;
        [[nodiscard]] const JsonValue* Find(JsonStringView key) const noexcept;
        bool                           Set(JsonStringView key, const JsonValue& value) noexcept;
        bool                           BuildIndex() noexcept;

        NGIN::Containers::Vector<JsonMember, JsonAllocator> members;

    private:
        using IndexMap = NGIN::Containers::FlatHashMap<JsonStringView,
                                                       UIntSize,
                                                       std::hash<JsonStringView>,
                                                       std::equal_to<JsonStringView>,
                                                       JsonAllocator>;

        JsonAllocator m_allocator;
        IndexMap*     m_index {nullptr};
    };

    /// @brief JSON document owning an arena for parsed nodes.
    class NGIN_BASE_API JsonDocument
    {
    public:
        explicit JsonDocument(UIntSize arenaBytes);

        JsonDocument(JsonDocument&&) noexcept            = default;
        JsonDocument& operator=(JsonDocument&&) noexcept = default;
        JsonDocument(const JsonDocument&)                = delete;
        JsonDocument& operator=(const JsonDocument&)     = delete;

        [[nodiscard]] JsonValue&       Root() noexcept { return m_root; }
        [[nodiscard]] const JsonValue& Root() const noexcept { return m_root; }

        [[nodiscard]] JsonAllocator Allocator() noexcept { return JsonAllocator {m_arena}; }
        [[nodiscard]] JsonArena&    Arena() noexcept { return m_arena; }
        void                        AdoptInput(NGIN::Containers::Vector<NGIN::Byte>&& input) noexcept { m_inputStorage = std::move(input); }
        [[nodiscard]] JsonStringView InternString(JsonStringView value) noexcept;

    private:
        using InternMap = NGIN::Containers::FlatHashMap<JsonStringView,
                                                       JsonStringView,
                                                       std::hash<JsonStringView>,
                                                       std::equal_to<JsonStringView>,
                                                       JsonAllocator>;

        JsonArena                            m_arena;
        JsonValue                            m_root {};
        NGIN::Containers::Vector<NGIN::Byte> m_inputStorage {};
        InternMap*                           m_interner {nullptr};
    };
}// namespace NGIN::Serialization

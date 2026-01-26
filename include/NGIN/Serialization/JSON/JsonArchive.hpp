#pragma once

#include <NGIN/Serialization/Archive.hpp>
#include <NGIN/Serialization/JSON/JsonTypes.hpp>

#include <string_view>

namespace NGIN::Serialization
{
    /// @brief JSON archive for DOM-backed serialization.
    class NGIN_BASE_API JsonArchive final : public Archive
    {
    public:
        explicit JsonArchive(JsonDocument& document) noexcept;
        explicit JsonArchive(const JsonDocument& document) noexcept;

        bool BeginObject() noexcept;
        bool EndObject() noexcept;
        bool BeginArray() noexcept;
        bool EndArray() noexcept;

        bool Key(std::string_view key) noexcept;
        bool NextElement() noexcept;

        bool Null() noexcept;
        bool Value(bool& value) noexcept;
        bool Value(F64& value) noexcept;
        bool Value(std::string_view& value) noexcept;
        bool Value(std::string_view value) noexcept;

    private:
        struct Frame
        {
            enum class Kind : UInt8
            {
                Object,
                Array,
            };

            Kind             kind {Kind::Object};
            const JsonValue* readValue {nullptr};
            JsonValue*       writeValue {nullptr};
            UIntSize         index {0};
        };

        JsonValue*       ResolveWriteTarget() noexcept;
        const JsonValue* ResolveReadTarget() noexcept;
        JsonObject*      CreateObject() noexcept;
        JsonArray*       CreateArray() noexcept;

        const JsonValue* m_rootRead {nullptr};
        JsonValue*       m_rootWrite {nullptr};
        const JsonValue* m_pendingRead {nullptr};
        JsonValue*       m_pendingWrite {nullptr};
        JsonDocument*    m_document {nullptr};

        NGIN::Containers::Vector<Frame> m_stack;
    };
}// namespace NGIN::Serialization

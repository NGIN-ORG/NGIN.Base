#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/JSON/JsonTypes.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace NGIN::Serialization::JSON
{
    enum class BuildErrorCode : UInt8
    {
        InvalidHandle,
        InvalidString,
        DuplicateKey,
        NodeLimitExceeded,
        MemberLimitExceeded,
        MemoryLimitExceeded,
        OutOfMemory,
        AlreadyFinished,
    };

    struct BuildDiagnostic
    {
        BuildErrorCode    code {BuildErrorCode::InvalidHandle};
        NGIN::Text::String message {};
    };

    struct ObjectMember
    {
        std::string_view key {};
        NodeId           value {};
    };

    /// @brief Mutable construction facade that freezes into an immutable JSON document.
    class NGIN_BASE_API Builder
    {
    public:
        explicit Builder(const ParseLimits& limits = {}, const ParseResources& resources = {});
        ~Builder();

        Builder(Builder&&) noexcept;
        Builder& operator=(Builder&&) noexcept;
        Builder(const Builder&)            = delete;
        Builder& operator=(const Builder&) = delete;

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Null();
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Bool(bool value);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Int(Int64 value);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> UInt(UInt64 value);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Double(F64 value);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> String(std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Array(std::span<const NodeId> values);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
        Object(std::span<const ObjectMember> members);

        [[nodiscard]] NGIN::Utilities::Expected<Document, BuildDiagnostic> Finish(NodeId root);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::Serialization::JSON

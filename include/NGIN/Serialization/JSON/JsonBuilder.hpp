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
    /// @brief Failure category reported while building a JSON document.
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

    /// @brief Structured JSON build failure.
    struct BuildDiagnostic
    {
        BuildErrorCode     code {BuildErrorCode::InvalidHandle};
        NGIN::Text::String message {};
    };

    /// @brief Key and value-node reference used to construct an object.
    struct ObjectMember
    {
        std::string_view key {};
        NodeId           value {};
    };

    /// @brief Mutable construction facade that freezes into an immutable JSON document.
    class NGIN_SERIALIZATION_API Builder
    {
    public:
        /// @brief Creates a builder with explicit resource limits and allocation policy.
        explicit Builder(const ParseLimits& limits = {}, const ParseResources& resources = {});
        /// @brief Releases unfinished or finished construction state.
        ~Builder();

        /// @brief Transfers construction state from another builder.
        Builder(Builder&&) noexcept;
        /// @brief Replaces this builder with another builder's state.
        Builder& operator=(Builder&&) noexcept;
        /// @brief Builders are non-copyable because node identifiers are state-local.
        Builder(const Builder&) = delete;
        /// @brief Builders are non-copy-assignable because node identifiers are state-local.
        Builder& operator=(const Builder&) = delete;

        /// @brief Adds a JSON null node.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Null();
        /// @brief Adds a Boolean node.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Bool(bool value);
        /// @brief Adds a signed-integer node.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Int(Int64 value);
        /// @brief Adds an unsigned-integer node.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> UInt(UInt64 value);
        /// @brief Adds a floating-point node.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Double(F64 value);
        /// @brief Copies a decoded UTF-8 string into a new string node.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> String(std::string_view value);
        /// @brief Adds an array referencing existing builder-local nodes.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Array(std::span<const NodeId> values);
        /// @brief Adds an object referencing existing builder-local nodes.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
        Object(std::span<const ObjectMember> members);

        /// @brief Freezes construction state into an immutable document rooted at @p root.
        /// @note A builder can be finished only once.
        [[nodiscard]] NGIN::Utilities::Expected<Document, BuildDiagnostic> Finish(NodeId root);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::Serialization::JSON

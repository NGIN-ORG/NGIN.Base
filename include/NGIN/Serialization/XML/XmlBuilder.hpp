#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace NGIN::Serialization::XML
{
    /// @brief Failure category reported while building an XML document.
    enum class BuildErrorCode : UInt8
    {
        InvalidHandle,
        InvalidName,
        InvalidContent,
        NodeLimitExceeded,
        MemberLimitExceeded,
        MemoryLimitExceeded,
        OutOfMemory,
        AlreadyFinished,
    };

    /// @brief Structured XML build failure.
    struct BuildDiagnostic
    {
        BuildErrorCode     code {BuildErrorCode::InvalidHandle};
        NGIN::Text::String message {};
    };

    /// @brief Decoded attribute name and value used to construct an element.
    struct Attribute
    {
        std::string_view name {};
        std::string_view value {};
    };

    /// @brief Mutable construction facade that freezes into an immutable XML document.
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

        /// @brief Adds a decoded text node.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Text(std::string_view value);
        /// @brief Adds a CDATA node after validating its content.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> CData(std::string_view value);
        /// @brief Adds a comment node after validating its content.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Comment(std::string_view value);
        /// @brief Adds a processing instruction after validating its target and value.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
        ProcessingInstruction(std::string_view target, std::string_view value);
        /// @brief Adds an element referencing copied attributes and existing builder-local children.
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
        Element(std::string_view           name,
                std::span<const Attribute> attributes,
                std::span<const NodeId>    children);
        /// @brief Freezes construction state into an immutable document rooted at @p root.
        /// @note A builder can be finished only once and the root must identify an element.
        [[nodiscard]] NGIN::Utilities::Expected<Document, BuildDiagnostic> Finish(NodeId root);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::Serialization::XML

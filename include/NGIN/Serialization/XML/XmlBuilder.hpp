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

    struct BuildDiagnostic
    {
        BuildErrorCode     code {BuildErrorCode::InvalidHandle};
        NGIN::Text::String message {};
    };

    struct Attribute
    {
        std::string_view name {};
        std::string_view value {};
    };

    class NGIN_SERIALIZATION_API Builder
    {
    public:
        explicit Builder(const ParseLimits& limits = {}, const ParseResources& resources = {});
        ~Builder();
        Builder(Builder&&) noexcept;
        Builder& operator=(Builder&&) noexcept;
        Builder(const Builder&)            = delete;
        Builder& operator=(const Builder&) = delete;

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Text(std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> CData(std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Comment(std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
        ProcessingInstruction(std::string_view target, std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
                                                                           Element(std::string_view           name,
                                                                                   std::span<const Attribute> attributes,
                                                                                   std::span<const NodeId>    children);
        [[nodiscard]] NGIN::Utilities::Expected<Document, BuildDiagnostic> Finish(NodeId root);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::Serialization::XML

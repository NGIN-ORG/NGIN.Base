#include <NGIN/Serialization/XML/XmlBuilder.hpp>

#include "XmlDocumentInternal.hpp"

#include <NGIN/Text/Unicode/Utf8.hpp>

#include <limits>
#include <new>

namespace NGIN::Serialization::XML
{
    namespace
    {
        template<class T>
        [[nodiscard]] NGIN::Utilities::Expected<T, BuildDiagnostic>
        Failure(BuildErrorCode code, std::string_view message)
        {
            return NGIN::Utilities::Unexpected<BuildDiagnostic>(
                    BuildDiagnostic {.code = code, .message = NGIN::Text::String {message}});
        }

        [[nodiscard]] bool IsNameStart(unsigned char value) noexcept
        {
            return value == ':' || value == '_' ||
                   (value >= 'A' && value <= 'Z') ||
                   (value >= 'a' && value <= 'z') ||
                   value >= 0x80;
        }

        [[nodiscard]] bool IsNameContinue(unsigned char value) noexcept
        {
            return IsNameStart(value) || value == '-' || value == '.' ||
                   (value >= '0' && value <= '9');
        }

        [[nodiscard]] bool IsName(std::string_view name) noexcept
        {
            if (name.empty() || !NGIN::Text::Unicode::IsValidUtf8(name) ||
                !IsNameStart(static_cast<unsigned char>(name.front())))
                return false;
            for (UIntSize index = 1; index < name.size(); ++index)
            {
                if (!IsNameContinue(static_cast<unsigned char>(name[index])))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool IsXmlCharacter(UInt32 value) noexcept
        {
            return value == 0x09 || value == 0x0a || value == 0x0d ||
                   (value >= 0x20 && value <= 0xd7ff) ||
                   (value >= 0xe000 && value <= 0xfffd) ||
                   (value >= 0x10000 && value <= 0x10ffff);
        }

        [[nodiscard]] bool IsXmlText(std::string_view value) noexcept
        {
            UIntSize offset = 0;
            while (offset < value.size())
            {
                const auto decoded = NGIN::Text::Unicode::DecodeUtf8(value, offset);
                if (decoded.error != NGIN::Text::Unicode::EncodingError::None ||
                    !IsXmlCharacter(decoded.codePoint))
                    return false;
                offset += decoded.unitsConsumed;
            }
            return true;
        }
    }// namespace

    struct Builder::Impl
    {
        explicit Impl(const ParseLimits& limits, const ParseResources& resources)
            : state(std::make_unique<detail::DocumentState>(
                      BorrowedTextView {}, limits, resources))
        {
        }

        [[nodiscard]] bool Valid(NodeId id) const noexcept { return state && state->Node(id); }

        [[nodiscard]] detail::StringRef Copy(std::string_view value) noexcept
        {
            const auto copy = state->arena.CopyString(value);
            return {copy.data(), copy.size()};
        }

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
        Add(detail::NodeRecord node)
        {
            if (!state)
                return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "XML builder has already been finished");
            if (state->nodes.size() >= state->limits.maxNodes ||
                state->nodes.size() >= static_cast<UIntSize>((std::numeric_limits<UInt32>::max)()))
                return Failure<NodeId>(BuildErrorCode::NodeLimitExceeded, "XML builder node limit exceeded");
            try
            {
                state->nodes.push_back(node);
            } catch (const std::bad_alloc&)
            {
                return Failure<NodeId>(BuildErrorCode::OutOfMemory, "XML builder node allocation failed");
            }
            if (!state->WithinMemoryLimit())
            {
                state->nodes.pop_back();
                return Failure<NodeId>(BuildErrorCode::MemoryLimitExceeded, "XML builder memory limit exceeded");
            }
            return NodeId {static_cast<UInt32>(state->nodes.size() - 1)};
        }

        std::unique_ptr<detail::DocumentState> state;
    };

    Builder::Builder(const ParseLimits& limits, const ParseResources& resources)
        : m_impl(std::make_unique<Impl>(limits, resources))
    {
    }
    Builder::~Builder()                             = default;
    Builder::Builder(Builder&&) noexcept            = default;
    Builder& Builder::operator=(Builder&&) noexcept = default;

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::Text(std::string_view value)
    {
        if (!m_impl || !m_impl->state)
            return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "XML builder has already been finished");
        if (!IsXmlText(value))
            return Failure<NodeId>(BuildErrorCode::InvalidContent, "XML text contains an invalid character");
        const auto copy = m_impl->Copy(value);
        if (!value.empty() && copy.data == nullptr)
            return Failure<NodeId>(BuildErrorCode::OutOfMemory, "XML text allocation failed");
        return m_impl->Add(detail::NodeRecord {.kind = NodeKind::Text, .text = copy});
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::CData(std::string_view value)
    {
        if (!m_impl || !m_impl->state)
            return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "XML builder has already been finished");
        if (!IsXmlText(value))
            return Failure<NodeId>(BuildErrorCode::InvalidContent, "XML CDATA contains an invalid character");
        const auto copy = m_impl->Copy(value);
        if (!value.empty() && copy.data == nullptr)
            return Failure<NodeId>(BuildErrorCode::OutOfMemory, "XML CDATA allocation failed");
        return m_impl->Add(detail::NodeRecord {.kind = NodeKind::CData, .text = copy});
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::Comment(std::string_view value)
    {
        if (!IsXmlText(value) || value.find("--") != std::string_view::npos ||
            (!value.empty() && value.back() == '-'))
            return Failure<NodeId>(BuildErrorCode::InvalidContent, "XML comment content is invalid");
        if (!m_impl || !m_impl->state)
            return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "XML builder has already been finished");
        const auto copy = m_impl->Copy(value);
        if (!value.empty() && copy.data == nullptr)
            return Failure<NodeId>(BuildErrorCode::OutOfMemory, "XML comment allocation failed");
        return m_impl->Add(detail::NodeRecord {.kind = NodeKind::Comment, .text = copy});
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
    Builder::ProcessingInstruction(std::string_view target, std::string_view value)
    {
        if (!IsName(target) || !IsXmlText(value) ||
            (target.size() == 3 &&
             (target[0] == 'x' || target[0] == 'X') &&
             (target[1] == 'm' || target[1] == 'M') &&
             (target[2] == 'l' || target[2] == 'L')) ||
            value.find("?>") != std::string_view::npos)
            return Failure<NodeId>(BuildErrorCode::InvalidContent,
                                   "XML processing instruction target or content is invalid");
        if (!m_impl || !m_impl->state)
            return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "XML builder has already been finished");
        const auto targetCopy = m_impl->Copy(target);
        const auto valueCopy  = m_impl->Copy(value);
        if ((!target.empty() && targetCopy.data == nullptr) || (!value.empty() && valueCopy.data == nullptr))
            return Failure<NodeId>(BuildErrorCode::OutOfMemory, "XML processing instruction allocation failed");
        return m_impl->Add(detail::NodeRecord {
                .kind = NodeKind::ProcessingInstruction,
                .name = targetCopy,
                .text = valueCopy,
        });
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
    Builder::Element(std::string_view           name,
                     std::span<const Attribute> attributes,
                     std::span<const NodeId>    children)
    {
        if (!IsName(name))
            return Failure<NodeId>(BuildErrorCode::InvalidName, "XML element name is invalid");
        if (!m_impl || !m_impl->state)
            return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "XML builder has already been finished");
        constexpr auto maxIndex = (std::numeric_limits<UInt32>::max)();
        if (m_impl->state->attributes.size() > m_impl->state->limits.maxMembers ||
            attributes.size() > m_impl->state->limits.maxMembers - m_impl->state->attributes.size() ||
            m_impl->state->children.size() > m_impl->state->limits.maxMembers ||
            children.size() > m_impl->state->limits.maxMembers - m_impl->state->children.size() ||
            attributes.size() > maxIndex ||
            children.size() > maxIndex ||
            m_impl->state->attributes.size() > maxIndex - attributes.size() ||
            m_impl->state->children.size() > maxIndex - children.size() ||
            m_impl->state->elements.size() >= maxIndex)
            return Failure<NodeId>(BuildErrorCode::MemberLimitExceeded, "XML builder member limit exceeded");

        for (UIntSize left = 0; left < attributes.size(); ++left)
        {
            if (!IsName(attributes[left].name))
                return Failure<NodeId>(BuildErrorCode::InvalidName, "XML attribute name is invalid");
            if (!IsXmlText(attributes[left].value))
                return Failure<NodeId>(BuildErrorCode::InvalidContent,
                                       "XML attribute value contains an invalid character");
            for (UIntSize right = left + 1; right < attributes.size(); ++right)
            {
                if (attributes[left].name == attributes[right].name)
                    return Failure<NodeId>(BuildErrorCode::InvalidContent, "XML element has duplicate attributes");
            }
        }
        for (const auto child: children)
        {
            if (!m_impl->Valid(child))
                return Failure<NodeId>(BuildErrorCode::InvalidHandle, "XML element contains an invalid child handle");
        }

        const UIntSize attributeBegin = m_impl->state->attributes.size();
        const UIntSize childBegin     = m_impl->state->children.size();
        const UIntSize elementIndex   = m_impl->state->elements.size();
        try
        {
            m_impl->state->attributes.reserve(attributeBegin + attributes.size());
            for (const auto& attribute: attributes)
            {
                const auto key   = m_impl->Copy(attribute.name);
                const auto value = m_impl->Copy(attribute.value);
                if ((!attribute.name.empty() && key.data == nullptr) ||
                    (!attribute.value.empty() && value.data == nullptr))
                {
                    m_impl->state->attributes.resize(attributeBegin);
                    return Failure<NodeId>(BuildErrorCode::OutOfMemory, "XML attribute allocation failed");
                }
                m_impl->state->attributes.push_back(detail::AttributeRecord {.name = key, .value = value});
            }
            m_impl->state->children.insert(m_impl->state->children.end(), children.begin(), children.end());
            const auto elementName = m_impl->Copy(name);
            if (!name.empty() && elementName.data == nullptr)
            {
                m_impl->state->attributes.resize(attributeBegin);
                m_impl->state->children.resize(childBegin);
                return Failure<NodeId>(BuildErrorCode::OutOfMemory, "XML element name allocation failed");
            }
            m_impl->state->elements.push_back(detail::ElementRecord {
                    .name       = elementName,
                    .attributes = detail::Range {
                            static_cast<UInt32>(attributeBegin),
                            static_cast<UInt32>(attributes.size()),
                    },
                    .children = detail::Range {
                            static_cast<UInt32>(childBegin),
                            static_cast<UInt32>(children.size()),
                    },
            });
        } catch (const std::bad_alloc&)
        {
            m_impl->state->attributes.resize(attributeBegin);
            m_impl->state->children.resize(childBegin);
            return Failure<NodeId>(BuildErrorCode::OutOfMemory, "XML element allocation failed");
        }

        auto result = m_impl->Add(detail::NodeRecord {
                .kind    = NodeKind::Element,
                .element = static_cast<UInt32>(elementIndex),
        });
        if (!result)
        {
            m_impl->state->elements.pop_back();
            m_impl->state->attributes.resize(attributeBegin);
            m_impl->state->children.resize(childBegin);
        }
        return result;
    }

    NGIN::Utilities::Expected<Document, BuildDiagnostic> Builder::Finish(NodeId root)
    {
        if (!m_impl || !m_impl->state)
            return Failure<Document>(BuildErrorCode::AlreadyFinished, "XML builder has already been finished");
        const auto* node = m_impl->state->Node(root);
        if (!node || node->kind != NodeKind::Element)
            return Failure<Document>(BuildErrorCode::InvalidHandle, "XML document root must be an element");
        m_impl->state->root = root;
        try
        {
            m_impl->state->FinalizeViews();
        } catch (const std::bad_alloc&)
        {
            return Failure<Document>(BuildErrorCode::OutOfMemory, "XML view allocation failed");
        }
        if (!m_impl->state->WithinMemoryLimit())
            return Failure<Document>(BuildErrorCode::MemoryLimitExceeded, "XML builder memory limit exceeded");
        return detail::DocumentAccess::MakeDocument(std::move(m_impl->state));
    }
}// namespace NGIN::Serialization::XML

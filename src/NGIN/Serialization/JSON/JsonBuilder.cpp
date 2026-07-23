#include <NGIN/Serialization/JSON/JsonBuilder.hpp>

#include "JsonDocumentInternal.hpp"

#include <NGIN/Text/Unicode/Utf8.hpp>

#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace NGIN::Serialization::JSON
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
    }// namespace

    struct Builder::Impl
    {
        explicit Impl(const ParseLimits& parseLimits, const ParseResources& resources)
            : state(std::make_unique<detail::DocumentState>(
                      BorrowedTextView {}, parseLimits, resources))
        {
        }

        [[nodiscard]] bool Valid(NodeId id) const noexcept
        {
            return state && state->Node(id);
        }

        [[nodiscard]] NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
        Add(detail::NodeRecord node)
        {
            if (!state)
                return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
            if (state->nodes.size() >= state->limits.maxNodes ||
                state->nodes.size() >= static_cast<UIntSize>((std::numeric_limits<UInt32>::max)()))
                return Failure<NodeId>(BuildErrorCode::NodeLimitExceeded, "JSON builder node limit exceeded");

            try
            {
                state->nodes.push_back(node);
            } catch (const std::bad_alloc&)
            {
                return Failure<NodeId>(BuildErrorCode::OutOfMemory, "JSON builder node allocation failed");
            }

            if (!state->WithinMemoryLimit())
            {
                state->nodes.pop_back();
                return Failure<NodeId>(BuildErrorCode::MemoryLimitExceeded, "JSON builder memory limit exceeded");
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

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::Null()
    {
        return m_impl ? m_impl->Add(detail::NodeRecord {.kind = ValueKind::Null})
                      : Failure<NodeId>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::Bool(bool value)
    {
        detail::NodeRecord node {.kind = ValueKind::Bool};
        node.payload.boolValue = value;
        return m_impl ? m_impl->Add(node)
                      : Failure<NodeId>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::Int(Int64 value)
    {
        detail::NodeRecord node {.kind = ValueKind::Int64};
        node.payload.signedValue = value;
        return m_impl ? m_impl->Add(node)
                      : Failure<NodeId>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::UInt(UInt64 value)
    {
        detail::NodeRecord node {.kind = ValueKind::UInt64};
        node.payload.unsignedValue = value;
        return m_impl ? m_impl->Add(node)
                      : Failure<NodeId>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::Double(F64 value)
    {
        if (!std::isfinite(value))
            return Failure<NodeId>(BuildErrorCode::InvalidHandle, "JSON numbers must be finite");
        detail::NodeRecord node {.kind = ValueKind::Double};
        node.payload.doubleValue = value;
        return m_impl ? m_impl->Add(node)
                      : Failure<NodeId>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::String(std::string_view value)
    {
        if (!m_impl || !m_impl->state)
            return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
        if (value.size() > m_impl->state->limits.maxDecodedStringBytes)
            return Failure<NodeId>(BuildErrorCode::MemoryLimitExceeded, "JSON string byte limit exceeded");
        if (!NGIN::Text::Unicode::IsValidUtf8(value))
            return Failure<NodeId>(BuildErrorCode::InvalidString, "JSON string is not valid UTF-8");

        const auto copy = m_impl->state->arena.CopyString(value);
        if (!value.empty() && copy.data() == nullptr)
            return Failure<NodeId>(BuildErrorCode::OutOfMemory, "JSON string allocation failed");

        detail::NodeRecord node {.kind = ValueKind::String};
        node.payload.stringValue = detail::StringRef {copy.data(), copy.size()};
        return m_impl->Add(node);
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic> Builder::Array(std::span<const NodeId> values)
    {
        if (!m_impl || !m_impl->state)
            return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
        for (const auto value: values)
        {
            if (!m_impl->Valid(value))
                return Failure<NodeId>(BuildErrorCode::InvalidHandle, "JSON array contains an invalid value handle");
        }

        const UIntSize begin = m_impl->state->elements.size();
        try
        {
            m_impl->state->elements.insert(m_impl->state->elements.end(), values.begin(), values.end());
        } catch (const std::bad_alloc&)
        {
            return Failure<NodeId>(BuildErrorCode::OutOfMemory, "JSON array allocation failed");
        }

        detail::NodeRecord node {.kind = ValueKind::Array};
        node.payload.rangeValue = detail::NodeRange {begin, values.size()};
        auto result             = m_impl->Add(node);
        if (!result)
            m_impl->state->elements.resize(begin);
        return result;
    }

    NGIN::Utilities::Expected<NodeId, BuildDiagnostic>
    Builder::Object(std::span<const ObjectMember> members)
    {
        if (!m_impl || !m_impl->state)
            return Failure<NodeId>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
        if (m_impl->state->members.size() > m_impl->state->limits.maxMembers ||
            members.size() > m_impl->state->limits.maxMembers - m_impl->state->members.size())
            return Failure<NodeId>(BuildErrorCode::MemberLimitExceeded, "JSON builder member limit exceeded");

        const UIntSize begin = m_impl->state->members.size();
        for (UIntSize left = 0; left < members.size(); ++left)
        {
            if (!NGIN::Text::Unicode::IsValidUtf8(members[left].key))
                return Failure<NodeId>(BuildErrorCode::InvalidString, "JSON object key is not valid UTF-8");
            for (UIntSize right = left + 1; right < members.size(); ++right)
            {
                if (members[left].key == members[right].key)
                    return Failure<NodeId>(BuildErrorCode::DuplicateKey,
                                           "JSON builder object contains a duplicate key");
            }
        }
        try
        {
            m_impl->state->members.reserve(begin + members.size());
            for (const auto& member: members)
            {
                if (!m_impl->Valid(member.value))
                {
                    m_impl->state->members.resize(begin);
                    return Failure<NodeId>(BuildErrorCode::InvalidHandle,
                                           "JSON object contains an invalid value handle");
                }
                const auto key = m_impl->state->arena.CopyString(member.key);
                if (!member.key.empty() && key.data() == nullptr)
                {
                    m_impl->state->members.resize(begin);
                    return Failure<NodeId>(BuildErrorCode::OutOfMemory, "JSON object key allocation failed");
                }
                m_impl->state->members.push_back(detail::MemberRecord {
                        .key   = detail::StringRef {key.data(), key.size()},
                        .value = member.value,
                });
            }
        } catch (const std::bad_alloc&)
        {
            m_impl->state->members.resize(begin);
            return Failure<NodeId>(BuildErrorCode::OutOfMemory, "JSON object allocation failed");
        }

        detail::NodeRecord node {.kind = ValueKind::Object};
        node.payload.rangeValue = detail::NodeRange {begin, members.size()};
        auto result             = m_impl->Add(node);
        if (!result)
            m_impl->state->members.resize(begin);
        return result;
    }

    NGIN::Utilities::Expected<Document, BuildDiagnostic> Builder::Finish(NodeId root)
    {
        if (!m_impl || !m_impl->state)
            return Failure<Document>(BuildErrorCode::AlreadyFinished, "JSON builder has already been finished");
        if (!m_impl->Valid(root))
            return Failure<Document>(BuildErrorCode::InvalidHandle, "JSON builder root handle is invalid");

        m_impl->state->root = root;
        try
        {
            m_impl->state->FinalizeViews();
        } catch (const std::bad_alloc&)
        {
            return Failure<Document>(BuildErrorCode::OutOfMemory, "JSON view allocation failed");
        }
        if (!m_impl->state->WithinMemoryLimit())
            return Failure<Document>(BuildErrorCode::MemoryLimitExceeded, "JSON builder memory limit exceeded");
        return detail::DocumentAccess::MakeDocument(std::move(m_impl->state));
    }
}// namespace NGIN::Serialization::JSON

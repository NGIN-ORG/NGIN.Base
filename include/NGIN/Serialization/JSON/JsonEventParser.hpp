#pragma once

#include <NGIN/Serialization/Core/IncrementalParse.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <algorithm>
#include <concepts>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace NGIN::Serialization::JSON
{
    enum class EventKind : UInt8
    {
        Null,
        Bool,
        Int64,
        UInt64,
        Double,
        String,
        StartArray,
        EndArray,
        StartObject,
        Key,
        EndObject,
    };

    struct Event
    {
        EventKind        kind {EventKind::Null};
        SourceSpan       span {};
        std::string_view text {};
        bool             boolValue {false};
        Int64            intValue {0};
        UInt64           uintValue {0};
        F64              doubleValue {0};
    };

    struct EventAction
    {
        bool   continueParsing {true};
        UInt64 consumerContext {0};

        [[nodiscard]] static constexpr EventAction Continue() noexcept { return {}; }
        [[nodiscard]] static constexpr EventAction Stop(UInt64 context = 0) noexcept
        {
            return {.continueParsing = false, .consumerContext = context};
        }
    };

    template<class Handler>
    concept EventHandler = requires(Handler& handler, const Event& event) {
        { handler(event) } -> std::same_as<EventAction>;
    };

    namespace detail
    {
        using EventCallback = EventAction (*)(void*, const Event&);

        [[nodiscard]] NGIN_SERIALIZATION_API NGIN::Utilities::Expected<void, ParseDiagnostic>
                                             ParseEventsContiguous(BorrowedTextView    input,
                                                                   void*               handlerContext,
                                                                   EventCallback       callback,
                                                                   ParseScratch&       scratch,
                                                                   const ParseOptions& options,
                                                                   const ParseLimits&  limits);
    }// namespace detail

    /// @brief Event delivery over one complete contiguous input.
    ///
    /// Unescaped strings may reference input. Decoded strings remain valid only
    /// for this call. Container-start and key spans cover their source tokens,
    /// rather than the complete value that has not yet been consumed. The API
    /// name deliberately does not imply incremental input.
    class EventParser
    {
    public:
        template<EventHandler Handler>
        [[nodiscard]] static NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseContiguous(BorrowedTextView    input,
                        Handler&            handler,
                        ParseScratch&       scratch,
                        const ParseOptions& options = {},
                        const ParseLimits&  limits  = {})
        {
            // KeepLast requires buffering a complete object so the prior value can
            // be replaced without emitting it. Retain the DOM path for this uncommon
            // normalization policy; all streaming-compatible policies use the
            // allocation-light direct parser.
            if (options.duplicateKeys == DuplicateKeyPolicy::KeepLast)
            {
                auto parsed = ParseBorrowed(input, scratch, options, limits);
                if (!parsed)
                    return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(parsed.Error()));
                return Emit(parsed.Value().Root(), handler);
            }

            return detail::ParseEventsContiguous(
                    input,
                    &handler,
                    [](void* context, const Event& event) -> EventAction {
                        return (*static_cast<Handler*>(context))(event);
                    },
                    scratch,
                    options,
                    limits);
        }

    private:
        template<EventHandler Handler>
        [[nodiscard]] static NGIN::Utilities::Expected<void, ParseDiagnostic>
        Deliver(const Event& event, Handler& handler)
        {
            const EventAction action = handler(event);
            if (action.continueParsing)
                return {};
            ParseDiagnostic diagnostic;
            diagnostic.code            = ParseErrorCode::HandlerRejected;
            diagnostic.span            = event.span;
            diagnostic.location.offset = event.span.begin;
            diagnostic.consumerContext = action.consumerContext;
            diagnostic.message         = "JSON event handler stopped parsing";
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(diagnostic));
        }

        template<EventHandler Handler>
        [[nodiscard]] static NGIN::Utilities::Expected<void, ParseDiagnostic>
        Emit(ValueView value, Handler& handler)
        {
            Event event {.span = value.Span()};
            switch (value.Kind())
            {
                case ValueKind::Null:
                    event.kind = EventKind::Null;
                    return Deliver(event, handler);
                case ValueKind::Bool:
                    event.kind      = EventKind::Bool;
                    event.boolValue = *value.TryBool();
                    return Deliver(event, handler);
                case ValueKind::Int64:
                    event.kind     = EventKind::Int64;
                    event.intValue = *value.TryInt64();
                    return Deliver(event, handler);
                case ValueKind::UInt64:
                    event.kind      = EventKind::UInt64;
                    event.uintValue = *value.TryUInt64();
                    return Deliver(event, handler);
                case ValueKind::Double:
                    event.kind        = EventKind::Double;
                    event.doubleValue = *value.TryDouble();
                    return Deliver(event, handler);
                case ValueKind::String:
                    event.kind = EventKind::String;
                    event.text = *value.TryString();
                    return Deliver(event, handler);
                case ValueKind::Array: {
                    event.kind     = EventKind::StartArray;
                    auto delivered = Deliver(event, handler);
                    if (!delivered)
                        return delivered;
                    const auto array = value.TryArray();
                    for (const auto child: *array)
                    {
                        auto result = Emit(child, handler);
                        if (!result)
                            return result;
                    }
                    event.kind = EventKind::EndArray;
                    return Deliver(event, handler);
                }
                case ValueKind::Object: {
                    event.kind     = EventKind::StartObject;
                    auto delivered = Deliver(event, handler);
                    if (!delivered)
                        return delivered;
                    const auto object = value.TryObject();
                    for (const auto member: *object)
                    {
                        Event key {.kind = EventKind::Key,
                                   .span = member.Span(),
                                   .text = member.Key()};
                        auto  keyResult = Deliver(key, handler);
                        if (!keyResult)
                            return keyResult;
                        auto result = Emit(member.Value(), handler);
                        if (!result)
                            return result;
                    }
                    event.kind = EventKind::EndObject;
                    return Deliver(event, handler);
                }
            }
            ParseDiagnostic diagnostic;
            diagnostic.code    = ParseErrorCode::InvalidToken;
            diagnostic.message = "Unknown JSON event value";
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(diagnostic));
        }
    };

    /// @brief Chunk-fed JSON event parser with stable global limits and source offsets.
    /// @details Input is retained until a complete document validates, then events are
    /// emitted exactly once through the contiguous parser. Event text views are valid
    /// only for the handler invocation and must be copied if retained.
    template<EventHandler Handler>
    class IncrementalEventParser
    {
    public:
        IncrementalEventParser(
                Handler&            handler,
                ParseScratch&       scratch,
                const ParseOptions& options = {},
                const ParseLimits&  limits  = {},
                const SourceId      source  = {})
            : m_handler(&handler), m_scratch(&scratch), m_options(options), m_limits(limits), m_source(source)
        {
        }

        [[nodiscard]] IncrementalParseResult Feed(const std::string_view chunk)
        {
            if (m_complete || m_error)
                return InvalidState("JSON incremental parser requires Reset before another document");
            const UIntSize effectiveLimit = (std::min) (m_limits.maxInputBytes, m_limits.maxTotalMemoryBytes);
            if (chunk.size() > effectiveLimit || m_buffer.size() > effectiveLimit - chunk.size())
                return Fail(MakeDiagnostic(ParseErrorCode::LimitExceeded, "JSON incremental input limit exceeded"));

            try
            {
                m_buffer.append(chunk);
            } catch (const std::bad_alloc&)
            {
                return Fail(MakeDiagnostic(ParseErrorCode::OutOfMemory, "JSON incremental input allocation failed"));
            }
            return {.status = IncrementalParseStatus::NeedMoreInput};
        }

        [[nodiscard]] IncrementalParseResult Feed(const std::span<const Byte> chunk)
        {
            return Feed(std::string_view {reinterpret_cast<const char*>(chunk.data()), chunk.size()});
        }

        [[nodiscard]] IncrementalParseResult Finish()
        {
            if (m_complete)
                return {.status = IncrementalParseStatus::Complete};
            if (m_error)
                return {.status = IncrementalParseStatus::Error, .diagnostic = m_diagnostic};
            return TryComplete();
        }

        void Reset() noexcept
        {
            m_buffer.clear();
            m_complete = false;
            m_error    = false;
            m_diagnostic.reset();
        }

        [[nodiscard]] UIntSize TotalBytes() const noexcept { return m_buffer.size(); }
        [[nodiscard]] bool     IsComplete() const noexcept { return m_complete; }

    private:
        [[nodiscard]] ParseDiagnostic MakeDiagnostic(const ParseErrorCode code, const std::string_view message) const
        {
            ParseDiagnostic diagnostic;
            diagnostic.code            = code;
            diagnostic.location.offset = m_buffer.size();
            diagnostic.span            = {.source = m_source, .begin = m_buffer.size(), .end = m_buffer.size()};
            diagnostic.message         = message;
            return diagnostic;
        }

        [[nodiscard]] IncrementalParseResult InvalidState(const std::string_view message)
        {
            return Fail(MakeDiagnostic(ParseErrorCode::InvalidDocumentStructure, message));
        }

        [[nodiscard]] IncrementalParseResult Fail(ParseDiagnostic diagnostic)
        {
            m_error      = true;
            m_diagnostic = std::move(diagnostic);
            return {.status = IncrementalParseStatus::Error, .diagnostic = m_diagnostic};
        }

        [[nodiscard]] IncrementalParseResult TryComplete()
        {
            const auto input            = BorrowedTextView {m_buffer, m_source};
            auto       completionLimits = m_limits;
            completionLimits.maxTotalMemoryBytes -= m_buffer.size();
            auto validated = ParseBorrowed(input, *m_scratch, m_options, completionLimits);
            if (!validated)
                return Fail(std::move(validated.Error()));

            UIntSize eventCount = 0;
            auto     forwarding = [this, &eventCount](const Event& event) {
                ++eventCount;
                return (*m_handler)(event);
            };
            auto emitted = EventParser::ParseContiguous(
                    input, forwarding, *m_scratch, m_options, completionLimits);
            if (!emitted)
                return Fail(std::move(emitted.Error()));
            m_complete = true;
            return {.status = IncrementalParseStatus::Complete, .eventsProduced = eventCount};
        }

        Handler*                       m_handler {nullptr};
        ParseScratch*                  m_scratch {nullptr};
        ParseOptions                   m_options {};
        ParseLimits                    m_limits {};
        SourceId                       m_source {};
        std::string                    m_buffer {};
        bool                           m_complete {false};
        bool                           m_error {false};
        std::optional<ParseDiagnostic> m_diagnostic {};
    };
}// namespace NGIN::Serialization::JSON

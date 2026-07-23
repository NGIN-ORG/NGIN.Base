#pragma once

#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <concepts>
#include <string_view>

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

        [[nodiscard]] NGIN_BASE_API NGIN::Utilities::Expected<void, ParseDiagnostic>
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
                    for (const auto child: *value.TryArray())
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
                    for (const auto member: *value.TryObject())
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
}// namespace NGIN::Serialization::JSON

#pragma once

#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <concepts>
#include <string_view>

namespace NGIN::Serialization::XML
{
    enum class EventKind : UInt8
    {
        StartElement, Attribute, Text, CData, Comment,
        ProcessingInstruction, EndElement,
    };

    struct Event
    {
        EventKind        kind {EventKind::Text};
        SourceSpan       span {};
        std::string_view name {};
        std::string_view value {};
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

    /// @brief Event delivery over one complete contiguous XML input.
    class EventParser
    {
    public:
        template<EventHandler Handler>
        [[nodiscard]] static NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseContiguous(BorrowedTextView input,
                        Handler& handler,
                        ParseScratch& scratch,
                        const ParseOptions& options = {},
                        const ParseLimits& limits = {})
        {
            auto parsed = ParseBorrowed(input, scratch, options, limits);
            if (!parsed)
                return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(parsed.Error()));
            return EmitElement(parsed.Value().Root(), handler);
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
            diagnostic.code = ParseErrorCode::HandlerRejected;
            diagnostic.span = event.span;
            diagnostic.location.offset = event.span.begin;
            diagnostic.consumerContext = action.consumerContext;
            diagnostic.message = "XML event handler stopped parsing";
            return NGIN::Utilities::Unexpected<ParseDiagnostic>(std::move(diagnostic));
        }

        template<EventHandler Handler>
        [[nodiscard]] static NGIN::Utilities::Expected<void, ParseDiagnostic>
        EmitElement(ElementView element, Handler& handler)
        {
            Event event {.kind = EventKind::StartElement,
                         .span = element.Span(),
                         .name = element.Name()};
            auto delivered = Deliver(event, handler);
            if (!delivered)
                return delivered;
            for (const auto attribute: element.Attributes())
            {
                Event attributeEvent {.kind = EventKind::Attribute,
                                      .span = attribute.Span(),
                                      .name = attribute.Name(),
                                      .value = attribute.Value()};
                auto result = Deliver(attributeEvent, handler);
                if (!result)
                    return result;
            }
            for (const auto child: element.Children())
            {
                if (const auto nested = child.TryElement())
                {
                    auto result = EmitElement(*nested, handler);
                    if (!result)
                        return result;
                    continue;
                }
                Event childEvent {.span = child.Span(),
                                  .name = child.Name(),
                                  .value = child.TryText().value_or(std::string_view {})};
                switch (child.Kind())
                {
                    case NodeKind::Text: childEvent.kind = EventKind::Text; break;
                    case NodeKind::CData: childEvent.kind = EventKind::CData; break;
                    case NodeKind::Comment: childEvent.kind = EventKind::Comment; break;
                    case NodeKind::ProcessingInstruction:
                        childEvent.kind = EventKind::ProcessingInstruction;
                        break;
                    case NodeKind::Element: break;
                }
                auto result = Deliver(childEvent, handler);
                if (!result)
                    return result;
            }
            event.kind = EventKind::EndElement;
            return Deliver(event, handler);
        }
    };
}// namespace NGIN::Serialization::XML

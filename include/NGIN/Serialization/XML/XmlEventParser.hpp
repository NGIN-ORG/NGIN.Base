#pragma once

#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <concepts>
#include <string_view>

namespace NGIN::Serialization::XML
{
    enum class EventKind : UInt8
    {
        StartElement,
        Attribute,
        Text,
        CData,
        Comment,
        ProcessingInstruction,
        EndElement,
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

    /// @brief Event delivery over one complete contiguous XML input.
    ///
    /// Views into the borrowed input follow the input lifetime. Views produced
    /// by entity decoding or line-ending normalization are valid only for the
    /// duration of the handler invocation and must be copied if retained.
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
    };
}// namespace NGIN::Serialization::XML

#pragma once

#include <NGIN/Serialization/Core/IncrementalParse.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <algorithm>
#include <concepts>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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

        [[nodiscard]] NGIN_SERIALIZATION_API NGIN::Utilities::Expected<void, ParseDiagnostic>
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

    /// @brief Chunk-fed XML event parser retaining source and parser state across feeds.
    /// @details A complete document is validated before any handler invocation, which
    /// prevents duplicate events when markup, entities, UTF-8, or CDATA span chunks.
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
                return InvalidState("XML incremental parser requires Reset before another document");
            const UIntSize effectiveLimit = (std::min) (m_limits.maxInputBytes, m_limits.maxTotalMemoryBytes);
            if (chunk.size() > effectiveLimit || m_buffer.size() > effectiveLimit - chunk.size())
                return Fail(MakeDiagnostic(ParseErrorCode::LimitExceeded, "XML incremental input limit exceeded"));
            try
            {
                m_buffer.append(chunk);
            } catch (const std::bad_alloc&)
            {
                return Fail(MakeDiagnostic(ParseErrorCode::OutOfMemory, "XML incremental input allocation failed"));
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
}// namespace NGIN::Serialization::XML

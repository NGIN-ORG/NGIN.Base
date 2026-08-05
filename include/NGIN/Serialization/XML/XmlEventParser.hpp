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
    /// @brief Kind of one XML streaming parse event.
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

    /// @brief One XML parse event with source range and decoded name/value payload.
    struct Event
    {
        EventKind        kind {EventKind::Text};
        SourceSpan       span {};
        std::string_view name {};
        std::string_view value {};
    };

    /// @brief Handler response controlling whether event delivery continues.
    struct EventAction
    {
        bool   continueParsing {true};
        UInt64 consumerContext {0};

        /// @brief Requests continued event delivery.
        [[nodiscard]] static constexpr EventAction Continue() noexcept { return {}; }
        /// @brief Stops parsing and records optional consumer context in the diagnostic.
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
        /// @brief Parses one complete borrowed input and synchronously delivers events.
        /// @note Decoded event views are valid only for the handler invocation.
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
        /// @brief Binds a handler, reusable scratch storage, parse policy, and source identity.
        /// @note The handler and scratch storage must outlive this parser.
        IncrementalEventParser(
                Handler&            handler,
                ParseScratch&       scratch,
                const ParseOptions& options = {},
                const ParseLimits&  limits  = {},
                const SourceId      source  = {})
            : m_handler(&handler), m_scratch(&scratch), m_options(options), m_limits(limits), m_source(source)
        {
        }

        /// @brief Appends a UTF-8 chunk for the current document.
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

        /// @brief Appends a byte chunk for the current document.
        [[nodiscard]] IncrementalParseResult Feed(const std::span<const Byte> chunk)
        {
            return Feed(std::string_view {reinterpret_cast<const char*>(chunk.data()), chunk.size()});
        }

        /// @brief Validates the accumulated document and emits its events exactly once.
        [[nodiscard]] IncrementalParseResult Finish()
        {
            if (m_complete)
                return {.status = IncrementalParseStatus::Complete};
            if (m_error)
                return {.status = IncrementalParseStatus::Error, .diagnostic = m_diagnostic};
            return TryComplete();
        }

        /// @brief Clears document state while retaining buffer capacity and parser bindings.
        void Reset() noexcept
        {
            m_buffer.clear();
            m_complete = false;
            m_error    = false;
            m_diagnostic.reset();
        }

        /// @brief Returns the number of accumulated input bytes.
        [[nodiscard]] UIntSize TotalBytes() const noexcept { return m_buffer.size(); }
        /// @brief Returns whether the current document completed successfully.
        [[nodiscard]] bool IsComplete() const noexcept { return m_complete; }

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
            const BorrowedTextView input            = BorrowedTextView {m_buffer, m_source};
            ParseLimits            completionLimits = m_limits;
            completionLimits.maxTotalMemoryBytes -= m_buffer.size();
            NGIN::Utilities::Expected<BorrowedDocument, ParseDiagnostic> validated =
                    ParseBorrowed(input, *m_scratch, m_options, completionLimits);
            if (!validated)
                return Fail(std::move(validated.Error()));

            UIntSize eventCount = 0;
            auto     forwarding = [this, &eventCount](const Event& event) {
                ++eventCount;
                return (*m_handler)(event);
            };
            NGIN::Utilities::Expected<void, ParseDiagnostic> emitted = EventParser::ParseContiguous(
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

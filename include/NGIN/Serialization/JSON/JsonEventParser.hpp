#pragma once

#include <NGIN/Serialization/Core/IncrementalParse.hpp>
#include <NGIN/Serialization/Core/ParseScratch.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <algorithm>
#include <concepts>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace NGIN::Serialization::JSON
{
    /// @brief Kind of one JSON streaming parse event.
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

    /// @brief One JSON parse event with source range and kind-specific payload.
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
                                             ParseEventsContiguous(std::string_view    input,
                                                                   void*               handlerContext,
                                                                   EventCallback       callback,
                                                                   ParseScratch&       scratch,
                                                                   const ParseOptions& options,
                                                                   const ParseLimits&  limits);
        class NGIN_SERIALIZATION_API IncrementalEngine
        {
        public:
            IncrementalEngine(void* handler, EventCallback callback, ParseScratch& scratch,
                              const ParseOptions& options, const ParseLimits& limits);
            ~IncrementalEngine();
            IncrementalEngine(const IncrementalEngine&)                              = delete;
            IncrementalEngine&                   operator=(const IncrementalEngine&) = delete;
            [[nodiscard]] IncrementalParseResult Feed(std::string_view chunk);
            [[nodiscard]] IncrementalParseResult Finish();
            void                                 Reset() noexcept;
            [[nodiscard]] UIntSize               TotalBytes() const noexcept;
            [[nodiscard]] UIntSize               BufferedBytes() const noexcept;
            [[nodiscard]] UIntSize               MemoryCommitted() const noexcept;
            [[nodiscard]] bool                   IsComplete() const noexcept;

        private:
            struct Impl;
            std::unique_ptr<Impl> m_impl;
        };
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
        /// @brief Parses one complete input and synchronously delivers events.
        /// @note Event text views are valid only for the handler invocation.
        template<EventHandler Handler>
        [[nodiscard]] static NGIN::Utilities::Expected<void, ParseDiagnostic>
        ParseContiguous(std::string_view    input,
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

    /// @brief Delivers events during Feed, retaining only unfinished tokens and open-container state.
    /// @details A later error does not retract delivered events. Copy event text if retaining it.
    /// JSON KeepLast buffers the document until Finish because later keys can replace earlier values.
    template<EventHandler Handler>
    class IncrementalEventParser
    {
    public:
        /// @brief Binds a handler and scratch storage, which must outlive the parser.
        IncrementalEventParser(Handler& handler, ParseScratch& scratch,
                               const ParseOptions& options = {}, const ParseLimits& limits = {})
            : m_engine(&handler, [](void* context, const Event& event) { return (*static_cast<Handler*>(context))(event); }, scratch, options, limits) {}
        /// @brief Consumes a chunk and reports the number of callbacks invoked by this call.
        [[nodiscard]] IncrementalParseResult Feed(std::string_view chunk) { return m_engine.Feed(chunk); }
        [[nodiscard]] IncrementalParseResult Feed(std::span<const Byte> chunk)
        {
            return Feed(std::string_view {reinterpret_cast<const char*>(chunk.data()), chunk.size()});
        }
        /// @brief Marks end of input and diagnoses incomplete tokens or containers. Idempotent on success.
        [[nodiscard]] IncrementalParseResult Finish() { return m_engine.Finish(); }
        /// @brief Resets document state and counters, retaining reusable storage.
        void                   Reset() noexcept { m_engine.Reset(); }
        [[nodiscard]] UIntSize TotalBytes() const noexcept { return m_engine.TotalBytes(); }
        /// @brief Bytes retained for an unfinished token (or a KeepLast document).
        [[nodiscard]] UIntSize BufferedBytes() const noexcept { return m_engine.BufferedBytes(); }
        /// @brief Retained dynamic parser and scratch allocation bytes; excludes fixed object storage.
        [[nodiscard]] UIntSize MemoryCommitted() const noexcept { return m_engine.MemoryCommitted(); }
        [[nodiscard]] bool     IsComplete() const noexcept { return m_engine.IsComplete(); }

    private:
        detail::IncrementalEngine m_engine;
    };
}// namespace NGIN::Serialization::JSON

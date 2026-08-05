#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Serialization/Core/TextSink.hpp>
#include <NGIN/Serialization/JSON/JsonWriter.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <string_view>
#include <vector>

namespace NGIN::Serialization::JSON
{
    /// @brief Stateful, allocation-reusing JSON writer for directly-authored output.
    ///
    /// Reset() retains stack capacity. The destination is never owned and must
    /// outlive the writer.
    class NGIN_SERIALIZATION_API StreamWriter
    {
    public:
        /// @brief Binds a non-owning output sink and formatting policy.
        explicit StreamWriter(TextSink sink, const WriteOptions& options = {});

        /// @brief Starts a new document with a replacement sink while retaining stack capacity.
        void Reset(TextSink sink) noexcept;

        /// @brief Begins an object value.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> BeginObject();
        /// @brief Ends the current object after all keys have values.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> EndObject();
        /// @brief Begins an array value.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> BeginArray();
        /// @brief Ends the current array.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> EndArray();
        /// @brief Writes an object key; the next operation must provide its value.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Key(std::string_view value);
        /// @brief Writes a null value.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Null();
        /// @brief Writes a Boolean value.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Bool(bool value);
        /// @brief Writes a signed-integer value.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Int64(Int64 value);
        /// @brief Writes an unsigned-integer value.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> UInt64(UInt64 value);
        /// @brief Writes a finite floating-point value.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Double(F64 value);
        /// @brief Writes a decoded UTF-8 string with JSON escaping.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> String(std::string_view value);
        /// @brief Validates that exactly one complete root value was written.
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Finish();

        /// @brief Returns the number of bytes accepted by the sink for the current document.
        [[nodiscard]] UIntSize BytesWritten() const noexcept { return m_bytesWritten; }

    private:
        enum class Container : UInt8
        {
            Object,
            Array,
        };

        struct Frame
        {
            Container kind {Container::Array};
            UIntSize  count {0};
            bool      awaitingValue {false};
        };

        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> BeforeValue();
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> CompleteScalar();
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Append(std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> AppendIndent(UIntSize depth);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> AppendEscaped(std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Fail(
                WriteErrorCode code, std::string_view message) const;

        TextSink           m_sink {};
        WriteOptions       m_options {};
        std::vector<Frame> m_stack {};
        UIntSize           m_bytesWritten {0};
        bool               m_hasRoot {false};
        bool               m_failed {false};
    };
}// namespace NGIN::Serialization::JSON

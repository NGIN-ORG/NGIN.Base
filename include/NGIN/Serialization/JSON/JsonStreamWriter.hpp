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
    class NGIN_BASE_API StreamWriter
    {
    public:
        explicit StreamWriter(TextSink sink, const WriteOptions& options = {});

        void Reset(TextSink sink) noexcept;

        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> BeginObject();
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> EndObject();
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> BeginArray();
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> EndArray();
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Key(std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Null();
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Bool(bool value);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Int64(Int64 value);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> UInt64(UInt64 value);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Double(F64 value);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> String(std::string_view value);
        [[nodiscard]] NGIN::Utilities::Expected<void, WriteDiagnostic> Finish();

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

        TextSink          m_sink {};
        WriteOptions      m_options {};
        std::vector<Frame> m_stack {};
        UIntSize          m_bytesWritten {0};
        bool              m_hasRoot {false};
        bool              m_failed {false};
    };
}// namespace NGIN::Serialization::JSON

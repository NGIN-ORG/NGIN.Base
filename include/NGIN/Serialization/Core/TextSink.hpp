#pragma once

#include <NGIN/Primitives.hpp>

#include <ostream>
#include <string>
#include <string_view>

namespace NGIN::Serialization
{
    /// @brief Minimal non-owning destination used by serialization writers.
    ///
    /// The sink and its context must outlive the writer. A false result means
    /// that the destination rejected the bytes; no exception crosses the sink
    /// boundary.
    struct TextSink
    {
        using WriteFunction = bool (*)(void*, std::string_view) noexcept;

        void*         context {nullptr};
        WriteFunction write {nullptr};

        [[nodiscard]] bool Write(std::string_view bytes) const noexcept
        {
            return write != nullptr && write(context, bytes);
        }
    };

    [[nodiscard]] inline TextSink MakeTextSink(std::string& output) noexcept
    {
        return TextSink {
                .context = &output,
                .write   = [](void* context, std::string_view bytes) noexcept {
                    try
                    {
                        static_cast<std::string*>(context)->append(bytes);
                        return true;
                    } catch (...)
                    {
                        return false;
                    }
                },
        };
    }

    [[nodiscard]] inline TextSink MakeTextSink(std::ostream& output) noexcept
    {
        return TextSink {
                .context = &output,
                .write   = [](void* context, std::string_view bytes) noexcept {
                    try
                    {
                        auto& stream = *static_cast<std::ostream*>(context);
                        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                        return static_cast<bool>(stream);
                    } catch (...)
                    {
                        return false;
                    }
                },
        };
    }
}// namespace NGIN::Serialization

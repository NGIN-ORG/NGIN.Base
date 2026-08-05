#pragma once

#include <NGIN/Primitives.hpp>

#include <limits>

namespace NGIN::Serialization
{
    /// @brief Resource limits shared by JSON and XML parsing.
    struct ParseLimits
    {
        UIntSize maxInputBytes {64ULL * 1024ULL * 1024ULL};
        UIntSize maxDepth {256};
        UIntSize maxNodes {4ULL * 1024ULL * 1024ULL};
        UIntSize maxMembers {4ULL * 1024ULL * 1024ULL};
        UIntSize maxDecodedStringBytes {64ULL * 1024ULL * 1024ULL};
        UIntSize maxTotalMemoryBytes {256ULL * 1024ULL * 1024ULL};

        /// @brief Returns a policy with every resource limit disabled.
        [[nodiscard]] static constexpr ParseLimits Unlimited() noexcept
        {
            constexpr UIntSize unlimited = (std::numeric_limits<UIntSize>::max)();
            return {
                    .maxInputBytes         = unlimited,
                    .maxDepth              = unlimited,
                    .maxNodes              = unlimited,
                    .maxMembers            = unlimited,
                    .maxDecodedStringBytes = unlimited,
                    .maxTotalMemoryBytes   = unlimited,
            };
        }
    };
}// namespace NGIN::Serialization

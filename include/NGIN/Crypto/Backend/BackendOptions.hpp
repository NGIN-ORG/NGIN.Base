#pragma once

#include <NGIN/Crypto/Backend/AlgorithmSet.hpp>
#include <NGIN/Primitives.hpp>

#include <string_view>

namespace NGIN::Crypto::Backend
{
    /// @brief Backend selection policy used when creating a neutral crypto context.
    enum class BackendPolicy : NGIN::UInt8
    {
        PlatformOnly,
        PackagesOnly,
        PreferPlatformThenPackages,
        PreferPackagesThenPlatform,
        RequireFipsCapable,
        RequireAlgorithmSet,
    };

    /// @brief Neutral crypto context construction options.
    struct BackendOptions
    {
        bool             requireSecureRandom {true};
        BackendPolicy    policy {BackendPolicy::PreferPackagesThenPlatform};
        AlgorithmSet     requiredAlgorithms {};
        std::string_view packageName {};
    };
}// namespace NGIN::Crypto::Backend

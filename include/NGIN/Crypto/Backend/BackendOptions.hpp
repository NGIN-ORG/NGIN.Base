#pragma once

namespace NGIN::Crypto::Backend
{
    /// @brief Neutral crypto context construction options.
    struct BackendOptions
    {
        bool requireSecureRandom {true};
    };
}// namespace NGIN::Crypto::Backend

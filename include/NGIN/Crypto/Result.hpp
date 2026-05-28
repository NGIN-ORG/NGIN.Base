#pragma once

#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::Crypto
{
    /// @brief Standard crypto result type for recoverable failures.
    template<class T>
    using CryptoExpected = NGIN::Utilities::Expected<T, CryptoError>;
}// namespace NGIN::Crypto

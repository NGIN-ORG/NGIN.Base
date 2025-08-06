#pragma once

#include <cstddef>
#include <vector>
#include <string>

namespace NGIN::Crypto
{

    // Constant-time comparison
    bool ConstantTimeEqual(const void* a, const void* b, size_t len);

    // Secure wipe/clear
    void SecureWipe(void* data, size_t len);

}// namespace NGIN::Crypto

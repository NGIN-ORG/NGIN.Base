#pragma once

#include <cstddef>
#include <vector>
#include <string>

namespace NGIN::Crypto
{

    // Constant-time comparison
    bool ConstantTimeEqual(const uint8_t* a, const uint8_t* b, size_t len)
    {
        if (!a || !b)
            return false;

        uint8_t result = 0;
        for (size_t i = 0; i < len; ++i)
        {
            result |= a[i] ^ b[i];
        }
        return result == 0;
    }

    // Secure wipe/clear
    void SecureWipe(uint8_t* data, size_t len)
    {
        if (!data)
            return;

        volatile uint8_t* p = data;
        while (len--)
        {
            *p++ = 0;
        }
    }

}// namespace NGIN::Crypto

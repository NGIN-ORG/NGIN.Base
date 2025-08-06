#pragma once

#include <cstddef>
#include <vector>
#include <stdexcept>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace NGIN::Crypto::Random
{

    inline void GetBytes(void* out, size_t len)
    {
        if (len == 0)
            return;
        if (BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        {
            throw std::runtime_error("BCryptGenRandom failed");
        }
    }

    inline std::vector<uint8_t> GetBytes(size_t len)
    {
        std::vector<uint8_t> buf(len);
        GetBytes(buf.data(), len);
        return buf;
    }

}// namespace NGIN::Crypto::Random

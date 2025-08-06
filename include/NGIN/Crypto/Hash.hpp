#pragma once

#include <string>
#include <vector>
#include <array>

namespace NGIN::Crypto
{

    // Example SHA256 interface (stub)
    struct SHA256
    {
        static constexpr size_t OutputSize = 32;
        static std::array<uint8_t, OutputSize> Hash(const void* data, size_t len);
        static std::string HashHex(const void* data, size_t len);
    };

}// namespace NGIN::Crypto

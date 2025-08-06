#pragma once

#include <string>
#include <vector>

namespace NGIN::Crypto
{

    // Encoding utilities
    std::string ToHex(const std::vector<uint8_t>& data);
    std::vector<uint8_t> FromHex(const std::string& hex);
    std::string ToBase64(const std::vector<uint8_t>& data);
    std::vector<uint8_t> FromBase64(const std::string& b64);
    // Add Base58, etc. as needed

}// namespace NGIN::Crypto

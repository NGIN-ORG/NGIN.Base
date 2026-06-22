#include <NGIN/Crypto/Encoding/Pem.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    auto                                    input = std::string_view {reinterpret_cast<const char*>(data), size};
    NGIN::Crypto::Encoding::PemParseOptions options;
    options.maxDecodedBytes     = 64u * 1024u;
    options.allowMultipleBlocks = true;
    (void) NGIN::Crypto::Encoding::ParsePem(input, options);
    return 0;
}

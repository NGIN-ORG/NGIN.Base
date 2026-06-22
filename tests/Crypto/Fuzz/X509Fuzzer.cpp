#include <NGIN/Crypto/Certificates/Certificate.hpp>

#include <NGIN/Crypto/Types.hpp>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    auto input = NGIN::Crypto::ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(data), size};
    (void) NGIN::Crypto::Certificates::ParseX509Certificate(input);
    return 0;
}

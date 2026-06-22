#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>
#include <NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp>

#include <NGIN/Crypto/Types.hpp>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    auto input = NGIN::Crypto::ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(data), size};
    (void) NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(input);
    (void) NGIN::Crypto::Keys::ParsePrivateKeyInfo(input);
    return 0;
}

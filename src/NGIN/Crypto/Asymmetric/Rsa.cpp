#include <NGIN/Crypto/Asymmetric/Rsa.hpp>

namespace NGIN::Crypto::Asymmetric
{
    CryptoExpected<ByteBuffer> SignRsaPssSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const RsaPssSha256SignInput&                input)
    {
        auto supported = context.EnsureSupports(SignatureAlgorithm::RsaPssSha256);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return context.RsaPssSha256Sign(input.privateKeyDer, input.message);
    }

    CryptoExpected<void> VerifyRsaPssSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const RsaPssSha256VerifyInput&              input) noexcept
    {
        auto supported = context.EnsureSupports(SignatureAlgorithm::RsaPssSha256);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return context.RsaPssSha256Verify(input.publicKeyDer, input.message, input.signature);
    }

    CryptoExpected<ByteBuffer> EncryptRsaOaepSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const RsaOaepSha256EncryptInput&            input)
    {
        auto supported = context.EnsureSupports(AsymmetricEncryptionAlgorithm::RsaOaepSha256);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return context.RsaOaepSha256Encrypt(input.publicKeyDer, input.plaintext, input.label);
    }

    CryptoExpected<ByteBuffer> DecryptRsaOaepSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const RsaOaepSha256DecryptInput&            input)
    {
        auto supported = context.EnsureSupports(AsymmetricEncryptionAlgorithm::RsaOaepSha256);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return context.RsaOaepSha256Decrypt(input.privateKeyDer, input.ciphertext, input.label);
    }
}// namespace NGIN::Crypto::Asymmetric

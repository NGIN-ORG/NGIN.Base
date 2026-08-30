#include <NGIN/Crypto/Tokens/TokenGenerator.hpp>

#include <NGIN/Crypto/Encoding/Base64Url.hpp>
#include <NGIN/Crypto/Encoding/Hex.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>

#include <utility>

namespace NGIN::Crypto::Tokens
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
        }

        [[nodiscard]] constexpr CryptoError PolicyRejected() noexcept
        {
            return CryptoError {CryptoErrorCode::PolicyRejected};
        }

        [[nodiscard]] CryptoExpected<void> ValidateTokenSize(
                NGIN::UIntSize byteLength,
                NGIN::UIntSize minimumEntropyBytes) noexcept
        {
            if (byteLength == 0)
            {
                return std::unexpected(InvalidArgument());
            }
            if (byteLength < minimumEntropyBytes)
            {
                return std::unexpected(PolicyRejected());
            }

            return {};
        }
    }// namespace

    CryptoExpected<ByteBuffer> GenerateBytes(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::UIntSize                              byteLength,
            NGIN::UIntSize                              minimumEntropyBytes)
    {
        auto valid = ValidateTokenSize(byteLength, minimumEntropyBytes);
        if (!valid.has_value())
        {
            return std::unexpected(std::move(valid).error());
        }

        auto output = MakeByteBuffer(byteLength);
        auto random = context.FillRandom(ByteSpan {output.data(), output.Size()});
        if (!random.has_value())
        {
            return std::unexpected(std::move(random).error());
        }

        return output;
    }

    CryptoExpected<SecureToken> GenerateToken(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const TokenOptions&                         options)
    {
        switch (options.encoding)
        {
            case TokenEncoding::Base64Url:
                return GenerateBase64Url(context, options);
            case TokenEncoding::Hex:
                return GenerateHex(context, options);
        }

        return std::unexpected(InvalidArgument());
    }

    CryptoExpected<SecureToken> GenerateHex(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const TokenOptions&                         options)
    {
        auto bytes = GenerateBytes(context, options.byteLength, options.minimumEntropyBytes);
        if (!bytes.has_value())
        {
            return std::unexpected(std::move(bytes).error());
        }

        auto encoded = NGIN::Crypto::Encoding::EncodeHex(ConstByteSpan {bytes.value().data(), bytes.value().Size()});
        if (!encoded.has_value())
        {
            return std::unexpected(std::move(encoded).error());
        }

        return SecureToken {std::move(encoded.value())};
    }

    CryptoExpected<SecureToken> GenerateBase64Url(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const TokenOptions&                         options)
    {
        auto bytes = GenerateBytes(context, options.byteLength, options.minimumEntropyBytes);
        if (!bytes.has_value())
        {
            return std::unexpected(std::move(bytes).error());
        }

        auto encoded = NGIN::Crypto::Encoding::EncodeBase64Url(ConstByteSpan {bytes.value().data(), bytes.value().Size()});
        if (!encoded.has_value())
        {
            return std::unexpected(std::move(encoded).error());
        }

        return SecureToken {std::move(encoded.value())};
    }
}// namespace NGIN::Crypto::Tokens

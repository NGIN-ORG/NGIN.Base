#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto
{
    const char* ToString(CryptoErrorCode code) noexcept
    {
        switch (code)
        {
            case CryptoErrorCode::None:
                return "none";
            case CryptoErrorCode::InvalidArgument:
                return "invalid argument";
            case CryptoErrorCode::OutputBufferTooSmall:
                return "output buffer too small";
            case CryptoErrorCode::InvalidKey:
                return "invalid key";
            case CryptoErrorCode::InvalidNonce:
                return "invalid nonce";
            case CryptoErrorCode::InvalidTag:
                return "invalid tag";
            case CryptoErrorCode::AuthenticationFailed:
                return "authentication failed";
            case CryptoErrorCode::UnsupportedAlgorithm:
                return "unsupported algorithm";
            case CryptoErrorCode::UnsupportedBackend:
                return "unsupported backend";
            case CryptoErrorCode::BackendUnavailable:
                return "backend unavailable";
            case CryptoErrorCode::EntropyUnavailable:
                return "entropy unavailable";
            case CryptoErrorCode::EncodingError:
                return "encoding error";
            case CryptoErrorCode::ParseError:
                return "parse error";
            case CryptoErrorCode::PolicyRejected:
                return "policy rejected";
            case CryptoErrorCode::InternalError:
                return "internal error";
        }

        return "unknown crypto error";
    }

    const char* CryptoError::Message() const noexcept
    {
        return ToString(m_code);
    }
}// namespace NGIN::Crypto

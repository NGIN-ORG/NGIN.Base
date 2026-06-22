#include <NGIN/Crypto/Crypto.hpp>

#include <array>
#include <iostream>
#include <string_view>

namespace
{
    using NGIN::Crypto::AeadAlgorithm;
    using NGIN::Crypto::AsymmetricEncryptionAlgorithm;
    using NGIN::Crypto::HashAlgorithm;
    using NGIN::Crypto::KdfAlgorithm;
    using NGIN::Crypto::KeyAgreementAlgorithm;
    using NGIN::Crypto::MacAlgorithm;
    using NGIN::Crypto::SignatureAlgorithm;
    using NGIN::Crypto::Backend::AlgorithmSupportInfo;
    using NGIN::Crypto::Backend::BackendKind;
    using NGIN::Crypto::Backend::CryptoContext;

    [[nodiscard]] constexpr std::string_view KindName(BackendKind kind) noexcept
    {
        switch (kind)
        {
            case BackendKind::Platform:
                return "platform";
            case BackendKind::ExternalPackage:
                return "external package";
            case BackendKind::Test:
                return "test";
        }

        return "unknown";
    }

    void PrintSupport(std::string_view label, AlgorithmSupportInfo support)
    {
        std::cout << "  " << label << ": " << (support.supported ? "yes" : "no") << " (" << support.reason << ")\n";
    }

    void PrintContext(const CryptoContext& context)
    {
        const auto& info = context.Info();

        std::cout << "Selected backend\n";
        std::cout << "  name: " << info.Name() << '\n';
        std::cout << "  kind: " << KindName(info.Kind()) << '\n';
        if (!info.Version().empty())
        {
            std::cout << "  version: " << info.Version() << '\n';
        }
        if (!info.Source().empty())
        {
            std::cout << "  source: " << info.Source() << '\n';
        }
        if (!info.BuildOption().empty())
        {
            std::cout << "  build option: " << info.BuildOption() << '\n';
        }
        if (!info.PackageName().empty())
        {
            std::cout << "  package: " << info.PackageName() << '\n';
        }
        std::cout << "  FIPS capable: " << (info.IsFipsCapable() ? "yes" : "no") << '\n';
        std::cout << "  FIPS validated: " << (info.IsFipsValidated() ? "yes" : "no") << "\n\n";

        std::cout << "Capabilities\n";
        PrintSupport("secure random", context.DescribeRandomSupport());
        PrintSupport("SHA-256", context.DescribeSupport(HashAlgorithm::Sha256));
        PrintSupport("HMAC-SHA256", context.DescribeSupport(MacAlgorithm::HmacSha256));
        PrintSupport("HKDF-SHA256", context.DescribeSupport(KdfAlgorithm::HkdfSha256));
        PrintSupport("PBKDF2-SHA256", context.DescribeSupport(KdfAlgorithm::Pbkdf2Sha256));
        PrintSupport("Argon2id", context.DescribeSupport(KdfAlgorithm::Argon2id));
        PrintSupport("AES-256-GCM", context.DescribeSupport(AeadAlgorithm::Aes256Gcm));
        PrintSupport("ChaCha20-Poly1305", context.DescribeSupport(AeadAlgorithm::ChaCha20Poly1305));
        PrintSupport("XChaCha20-Poly1305", context.DescribeSupport(AeadAlgorithm::XChaCha20Poly1305));
        PrintSupport("Ed25519", context.DescribeSupport(SignatureAlgorithm::Ed25519));
        PrintSupport("ECDSA P-256/SHA-256", context.DescribeSupport(SignatureAlgorithm::EcdsaP256Sha256));
        PrintSupport("RSA-PSS/SHA-256", context.DescribeSupport(SignatureAlgorithm::RsaPssSha256));
        PrintSupport("RSA-OAEP/SHA-256", context.DescribeSupport(AsymmetricEncryptionAlgorithm::RsaOaepSha256));
        PrintSupport("X25519", context.DescribeSupport(KeyAgreementAlgorithm::X25519));
    }
}// namespace

int main()
{
    auto selection = NGIN::Crypto::Backend::CreateContextWithDiagnostics();
    if (!selection.context)
    {
        std::cerr << "Crypto backend selection failed: " << selection.context.Error().Message() << '\n';
        for (NGIN::UIntSize i = 0; i < selection.diagnostics.Count(); ++i)
        {
            const auto& diagnostic = selection.diagnostics[i];
            std::cerr << "  " << diagnostic.backend.Name() << ": " << NGIN::Crypto::ToString(diagnostic.code)
                      << " (" << diagnostic.reason << ")\n";
        }
        return 1;
    }

    const auto& context = *selection.context;
    PrintContext(context);

    auto token = NGIN::Crypto::Tokens::GenerateBase64Url(context, {
                                                                          .byteLength          = 16,
                                                                          .minimumEntropyBytes = 16,
                                                                          .encoding            = NGIN::Crypto::Tokens::TokenEncoding::Base64Url,
                                                                  });
    if (!token)
    {
        std::cerr << "\nToken generation failed: " << token.Error().Message() << '\n';
        return 1;
    }

    std::cout << "\nGenerated 128-bit URL-safe token: " << token->Value() << '\n';
    return 0;
}

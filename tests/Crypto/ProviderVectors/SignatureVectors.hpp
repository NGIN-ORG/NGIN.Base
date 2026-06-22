#pragma once

#include <string_view>

namespace NGIN::Crypto::Tests::ProviderVectors
{
    struct Ed25519Vector
    {
        std::string_view privateKeyHex;
        std::string_view publicKeyHex;
        std::string_view messageHex;
        std::string_view signatureHex;
    };

    struct EcdsaP256Sha256Vector
    {
        std::string_view privateKeyHex;
        std::string_view publicKeyHex;
        std::string_view messageHex;
        std::string_view signatureHex;
    };

    struct RsaPssSha256Vector
    {
        std::string_view privateKeyDerHex;
        std::string_view publicKeyDerHex;
        std::string_view messageHex;
        std::string_view signatureHex;
    };

    inline constexpr Ed25519Vector ED25519_RFC_8032_TEST_1 {
            .privateKeyHex = "9d61b19deffd5a60ba844af492ec2cc4"
                             "4449c5697b326919703bac031cae7f60",
            .publicKeyHex  = "d75a980182b10ab7d54bfed3c964073a"
                             "0ee172f3daa62325af021a68f707511a",
            .messageHex    = "",
            .signatureHex  = "e5564300c360ac729086e2cc806e828a"
                             "84877f1eb8e5d974d873e06522490155"
                             "5fb8821590a33bacc61e39701cf9b46b"
                             "d25bf5f0595bbe24655141438e7a100b",
    };

    inline constexpr EcdsaP256Sha256Vector ECDSA_P256_SHA256_REGRESSION {
            .privateKeyHex = "4226e38603a92579e1c235162e616132"
                             "fa22591887fca61c95397dd5d2e03aad",
            .publicKeyHex  = "049fc6ca172784949aa46ad2e02ee24a"
                             "fa44f8fec4eff939bff2f737bb3ac8e1"
                             "412378f7b376343946136cc5357d6259"
                             "e89e3b867cfe5e41a10cf5a8028ede7840",
            .messageHex    = "4e47494e20454344534120502d323536"
                             "205348412d3235362070726f7669646572"
                             "20766563746f72",
            .signatureHex  = "d62c2d0fb80511496302798dfd800b35"
                             "384feb5be149e7c035e3fcd47532ea49"
                             "3476f395b081744f4305707efde1d76c"
                             "2899e23a443b6e6c447cb6389ddf29d1",
    };

    inline constexpr RsaPssSha256Vector RSA_PSS_SHA256_REGRESSION {
            .privateKeyDerHex = "308204bd020100300d06092a864886f70d0101010500048204a7308204a30201"
                                "000282010100bef45997affc429629de0bd99dae76780dce350a41c309ae2bc3"
                                "cdd9717df80db1251dfa9a46b476cada7923664d65051d54184ed9c335257845"
                                "50cb768ab6c90a23de45eb612b3edd6b774e0b198157410f2428a163b5c52b1d"
                                "30fb3398e750bac08dd2a9c88cfcd9c2f091bff9d8e59896eaffc8f01c1fb0b0"
                                "cea7bc1d1f5fe2f57f82525246bc078fbaab9dbf84ad3f888ba70b9ca855dcb5"
                                "827a8b1f0af47eb17aa3b24b90a5272235da3b4b9fd7a34d8d73ae70f1829dd9"
                                "b303b08967ec42feaaf92f9ae20d223f590ad0a724257e67eef1001c359f3f98"
                                "894ea096a5804460576c56890d8532fc78940dd9c6445f16e73f241e2a3af295"
                                "00a45ae550970203010001028201004a8ef3f0734986e24504ab11d42c8b9b42"
                                "a0b23b080454624a3a50c4c7388d432358bfc4daba22ba83681c8682ea533855"
                                "0441dd7fcf658ff4cef3c60dca09a6a3abef9cd3540463ae66c8959365e9079d"
                                "a280d6d0753343021b8ac57a9708329048a0d7916d7a073c2b8e6d0a4d4eb00f"
                                "0d56427b3dad6d7e3afae5a960a19fcc173744eb6bfe18b9b5bd41b4eb204ccb"
                                "3bb60c3e35b45a601e44ac93509d2cc966c15b65167cbf77333d7c6110e6361c"
                                "e4fd30554b84a9cdb52a5afa3b82194c6b4f7e23b6a3d6e8abba29c828a3ef73"
                                "cb84b039b9d96fd4b8efd50fe6373bd6658d67e63696f7bff7377753d71aa486"
                                "53186293a1826390f960dc037c0dc902818100f84869a15c8736cff8d831aff9"
                                "4280fa753894cfbea144727968dc1715c81ccc7d480b3866deac5bcebf22cf8b"
                                "b87a4cea5eb9995ef0f67aae4340f9d506e3620240593973b256f1aaa8d5958c"
                                "cd3ed3ee3b90bbf2dc354cafe2134a940c7c4d1fe2e675219cb487ac0af00c05"
                                "2aa011fc22895e46d505e817bf847d3902bdeb02818100c4e3c67e47e3ad766d"
                                "cb0b36e7ffbf999588dda920d7f307689d81bbfc8bb7d43428827eedb9ed2e48"
                                "be4446d60efcb1b7d4d876271583e816a79f49594f1cc1e441f25f9c53b6c2b4"
                                "32979c9426c492d80b639504d35cea5d71869cead7a9b4577ceedaf03a94551a"
                                "fcc8a9dafaefdb7083a194abe75464bfb67bba8ff11105028180067d560d7aae"
                                "aa171b89031b77676e999d50c24affcf954b6cb2f9f5bc830385b0cc9e467621"
                                "e1dd7074fc82f242fb276184e25308f141700978068dc12cb1ead0e63faec9c0"
                                "6297647f8f3d9e758aebdd313f623a41d93ecce61adcfb2bac6013b24995cc30"
                                "1d63e27252bdedb0a48873cfaf49808e76de0f28d95cb08dcacb02818073ed99"
                                "a6293609a0bdacdc018c40add40493fdcfe3a2c35a0d63104c5bbaf8965dcfac"
                                "66881afd684f3498870ceaee26c612f111409a0e7ccd3a0d33e6fe09f4b02d94"
                                "446f8b6b04e18d186ffd5b864ae0227493fdaf65fa28e2fb5bd17b0255495e2a"
                                "55873536b9959659a0896b4f6769ac57ab6e3c5b18a7390f491e0fd6f1028181"
                                "00a86649baeb6e775515c7428047df74bb9ddff97d1dc2d96515fb9813b4a364"
                                "9ccfb0543ef7075980b8c305742f6e57e4253b81b236ed69f7d8c795ab73d460"
                                "a396bdb627ea0242d929e2b788a8a398ad3f1b12a1349be691ba516301187d5e"
                                "f65d9e0f2ef277b25372bf9800e5b86c3ba78c5f1cd7855be48147d162185acb"
                                "c8",
            .publicKeyDerHex  = "30820122300d06092a864886f70d01010105000382010f003082010a02820101"
                                "00bef45997affc429629de0bd99dae76780dce350a41c309ae2bc3cdd9717df8"
                                "0db1251dfa9a46b476cada7923664d65051d54184ed9c33525784550cb768ab6"
                                "c90a23de45eb612b3edd6b774e0b198157410f2428a163b5c52b1d30fb3398e7"
                                "50bac08dd2a9c88cfcd9c2f091bff9d8e59896eaffc8f01c1fb0b0cea7bc1d1f"
                                "5fe2f57f82525246bc078fbaab9dbf84ad3f888ba70b9ca855dcb5827a8b1f0a"
                                "f47eb17aa3b24b90a5272235da3b4b9fd7a34d8d73ae70f1829dd9b303b08967"
                                "ec42feaaf92f9ae20d223f590ad0a724257e67eef1001c359f3f98894ea096a5"
                                "804460576c56890d8532fc78940dd9c6445f16e73f241e2a3af29500a45ae550"
                                "970203010001",
            .messageHex       = "4e47494e205253412050535320616e64204f4145502070726f76696465722076"
                                "6563746f72",
            .signatureHex     = "93d6b49b8af2f4f7b30dba7669b93593881c15ec39b9cddd6e9f67522b72bde9"
                                "df4a38460ceb7840a6fddad617e3141dd346aac3650f1c76cb0b36ed0536d92e"
                                "abd4896a96612fb58441d8e99e1e61cb694a5d5cb15783be84b55aabaaaec6f4"
                                "38993a7125897429778c6cf3a962d46dce89e24dee887eb77f23f1bf96a30df4"
                                "0df5efc9258014a4c50d7b27500f0a4b0ab62addd0479df15b759112ed7337a5"
                                "1bbd00ff57d28c7a022a5d7d170b2851df408d031c3e6a8eb6c4f4fde8b28f2c"
                                "b59179dd614954a32d57892d0f0f70a0cf3840316f72ce0e30c0532bf8357945"
                                "e808e15f373ab2e2809a12cc6d7f9772fda4f8727ee27dd8933a5205b45a3dfb",
    };
}// namespace NGIN::Crypto::Tests::ProviderVectors

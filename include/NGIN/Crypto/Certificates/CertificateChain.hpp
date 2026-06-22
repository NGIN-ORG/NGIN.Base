#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Crypto/Certificates/Certificate.hpp>

namespace NGIN::Crypto::Certificates
{
    struct CertificateChain
    {
        NGIN::Containers::Vector<Certificate> certificates;
    };
}// namespace NGIN::Crypto::Certificates

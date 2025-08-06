#pragma once

#include <cstddef>
#include <vector>
#include <memory>

namespace NGIN::Crypto
{

    // SecureBuffer: zeroes memory on destruction
    class SecureBuffer
    {
    public:
        explicit SecureBuffer(size_t size);
        ~SecureBuffer();
        uint8_t* Data();
        const uint8_t* Data() const;
        size_t Size() const;

    private:
        std::unique_ptr<uint8_t[]> m_data;
        size_t m_size;
    };

}// namespace NGIN::Crypto

#pragma once

#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Types.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto::Memory
{
    /// @brief Move-only byte buffer that wipes its storage before releasing it.
    class SecureBuffer
    {
    public:
        SecureBuffer() = default;
        explicit SecureBuffer(NGIN::UIntSize size);
        explicit SecureBuffer(ConstByteSpan bytes);

        SecureBuffer(const SecureBuffer&)            = delete;
        SecureBuffer& operator=(const SecureBuffer&) = delete;

        SecureBuffer(SecureBuffer&& other) noexcept;
        SecureBuffer& operator=(SecureBuffer&& other) noexcept;

        ~SecureBuffer();

        [[nodiscard]] NGIN::UIntSize    Size() const noexcept;
        [[nodiscard]] bool              Empty() const noexcept;
        [[nodiscard]] NGIN::Byte*       Data() noexcept;
        [[nodiscard]] const NGIN::Byte* Data() const noexcept;
        [[nodiscard]] ByteSpan          AsBytes() noexcept;
        [[nodiscard]] ConstByteSpan     AsBytes() const noexcept;

        void Resize(NGIN::UIntSize size);
        void Clear() noexcept;

    private:
        void Wipe() noexcept;

        NGIN::Crypto::ByteBuffer m_bytes;
    };
}// namespace NGIN::Crypto::Memory

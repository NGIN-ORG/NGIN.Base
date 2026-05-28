#include <NGIN/Crypto/Memory/SecureBuffer.hpp>

#include <NGIN/Crypto/Memory/ZeroMemory.hpp>

#include <utility>

namespace NGIN::Crypto::Memory
{
    SecureBuffer::SecureBuffer(NGIN::UIntSize size)
        : m_bytes {NGIN::Crypto::MakeByteBuffer(size)}
    {
    }

    SecureBuffer::SecureBuffer(ConstByteSpan bytes)
        : m_bytes {NGIN::Crypto::MakeByteBuffer(bytes.size())}
    {
        for (NGIN::UIntSize i = 0; i < bytes.size(); ++i)
        {
            m_bytes[i] = bytes[i];
        }
    }

    SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
        : m_bytes {std::move(other.m_bytes)}
    {
    }

    SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Wipe();
            m_bytes = std::move(other.m_bytes);
        }

        return *this;
    }

    SecureBuffer::~SecureBuffer()
    {
        Wipe();
    }

    NGIN::UIntSize SecureBuffer::Size() const noexcept
    {
        return m_bytes.Size();
    }

    bool SecureBuffer::Empty() const noexcept
    {
        return m_bytes.Size() == 0;
    }

    NGIN::Byte* SecureBuffer::Data() noexcept
    {
        return m_bytes.data();
    }

    const NGIN::Byte* SecureBuffer::Data() const noexcept
    {
        return m_bytes.data();
    }

    ByteSpan SecureBuffer::AsBytes() noexcept
    {
        return ByteSpan {m_bytes.data(), m_bytes.Size()};
    }

    ConstByteSpan SecureBuffer::AsBytes() const noexcept
    {
        return ConstByteSpan {m_bytes.data(), m_bytes.Size()};
    }

    void SecureBuffer::Resize(NGIN::UIntSize size)
    {
        auto       replacement = NGIN::Crypto::MakeByteBuffer(size);
        const auto bytesToCopy = size < m_bytes.Size() ? size : m_bytes.Size();
        for (NGIN::UIntSize i = 0; i < bytesToCopy; ++i)
        {
            replacement[i] = m_bytes[i];
        }

        Wipe();
        m_bytes = std::move(replacement);
    }

    void SecureBuffer::Clear() noexcept
    {
        Wipe();
        m_bytes.Clear();
    }

    void SecureBuffer::Wipe() noexcept
    {
        SecureZero(ByteSpan {m_bytes.data(), m_bytes.Size()});
    }
}// namespace NGIN::Crypto::Memory

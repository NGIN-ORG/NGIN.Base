/// @file SecureBuffer.hpp
/// @brief Move-only dynamic byte storage that wipes released memory.
#pragma once

#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Types.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto::Memory
{
    /// @brief Move-only byte buffer that wipes its storage before releasing it.
    class NGIN_CRYPTO_API SecureBuffer
    {
    public:
        /// @brief Constructs an empty secure buffer.
        SecureBuffer() = default;
        /// @brief Constructs a zero-filled secure buffer of a requested size.
        explicit SecureBuffer(NGIN::UIntSize size);
        /// @brief Copies bytes into secure storage.
        explicit SecureBuffer(ConstByteSpan bytes);

        /// @brief Secure buffers are non-copyable to avoid implicit secret duplication.
        SecureBuffer(const SecureBuffer&) = delete;
        /// @brief Secure buffers are non-copy-assignable to avoid implicit secret duplication.
        SecureBuffer& operator=(const SecureBuffer&) = delete;

        /// @brief Transfers ownership of secure storage.
        SecureBuffer(SecureBuffer&& other) noexcept;
        /// @brief Wipes current storage and transfers ownership from another buffer.
        SecureBuffer& operator=(SecureBuffer&& other) noexcept;

        /// @brief Wipes and releases storage.
        ~SecureBuffer();

        /// @brief Returns the number of stored bytes.
        [[nodiscard]] NGIN::UIntSize Size() const noexcept;
        /// @brief Returns whether no bytes are stored.
        [[nodiscard]] bool Empty() const noexcept;
        /// @brief Returns mutable storage, or `nullptr` when empty.
        [[nodiscard]] NGIN::Byte* Data() noexcept;
        /// @brief Returns immutable storage, or `nullptr` when empty.
        [[nodiscard]] const NGIN::Byte* Data() const noexcept;
        /// @brief Returns a mutable span over the stored bytes.
        [[nodiscard]] ByteSpan AsBytes() noexcept;
        /// @brief Returns an immutable span over the stored bytes.
        [[nodiscard]] ConstByteSpan AsBytes() const noexcept;

        /// @brief Resizes storage, wiping bytes released by shrinking or reallocation.
        void Resize(NGIN::UIntSize size);
        /// @brief Wipes and releases all storage.
        void Clear() noexcept;

    private:
        void Wipe() noexcept;

        NGIN::Crypto::ByteBuffer m_bytes;
    };
}// namespace NGIN::Crypto::Memory

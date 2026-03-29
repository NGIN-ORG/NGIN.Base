// Checksum.hpp
// Implements various checksum algorithms in NGIN::Hashing::Checksum
#pragma once

#include <NGIN/Primitives.hpp>
#include <array>
#include <span>
#include <string_view>

namespace NGIN::Hashing::Checksum
{
    namespace detail
    {
        [[nodiscard]] inline std::span<const UInt8> ByteSpan(std::string_view text) noexcept
        {
            return std::span<const UInt8> {reinterpret_cast<const UInt8*>(text.data()), text.size()};
        }

        template<typename Derived>
        class ByteStateMixin
        {
        public:
            constexpr void Update(const UInt8* data, UIntSize len) noexcept
            {
                if (!data || len == 0)
                    return;

                static_cast<Derived*>(this)->Update(std::span<const UInt8> {data, static_cast<std::size_t>(len)});
            }

            constexpr void Update(std::string_view text) noexcept
            {
                if consteval
                {
                    for (unsigned char ch: text)
                        static_cast<Derived*>(this)->UpdateByte(static_cast<UInt8>(ch));
                }
                else
                {
                    static_cast<Derived*>(this)->Update(ByteSpan(text));
                }
            }
        };

        template<typename State>
        [[nodiscard]] constexpr auto Compute(std::span<const UInt8> data) noexcept -> typename State::value_type
        {
            State state;
            state.Update(data);
            return state.Finalize();
        }

        template<typename State>
        [[nodiscard]] constexpr auto Compute(const UInt8* data, UIntSize len) noexcept -> typename State::value_type
        {
            State state;
            if (data != nullptr && len != 0)
                state.Update(std::span<const UInt8> {data, static_cast<std::size_t>(len)});
            return state.Finalize();
        }

        template<typename State>
        [[nodiscard]] constexpr auto Compute(std::string_view text) noexcept -> typename State::value_type
        {
            State state;
            if consteval
            {
                for (unsigned char ch: text)
                    state.UpdateByte(static_cast<UInt8>(ch));
            }
            else
            {
                state.Update(ByteSpan(text));
            }
            return state.Finalize();
        }

        [[nodiscard]] constexpr UInt16 FoldUInt32ToUInt16(UInt32 sum) noexcept
        {
            while (sum >> 16)
                sum = (sum & 0xFFFFu) + (sum >> 16);
            return static_cast<UInt16>(sum);
        }

        [[nodiscard]] constexpr UInt16 ReadBigEndianUInt16(const UInt8* data) noexcept
        {
            return static_cast<UInt16>((static_cast<UInt16>(data[0]) << 8) | static_cast<UInt16>(data[1]));
        }
    }// namespace detail

    namespace Additive
    {
        class BSDState final : public detail::ByteStateMixin<BSDState>
        {
        public:
            using value_type = UInt16;
            using detail::ByteStateMixin<BSDState>::Update;

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                for (UInt8 byte: data)
                    UpdateByte(byte);
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                m_sum = static_cast<value_type>((m_sum >> 1) + ((m_sum & 1) << 15) + byte);
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                return m_sum;
            }

            constexpr void Reset() noexcept
            {
                m_sum = 0;
            }

            [[nodiscard]] constexpr value_type Raw() const noexcept
            {
                return m_sum;
            }

        private:
            value_type m_sum {0};
        };

        class SYSVState final : public detail::ByteStateMixin<SYSVState>
        {
        public:
            using value_type = UInt16;
            using raw_type   = UInt32;
            using detail::ByteStateMixin<SYSVState>::Update;

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                for (UInt8 byte: data)
                    UpdateByte(byte);
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                m_sum += byte;
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                return detail::FoldUInt32ToUInt16(m_sum);
            }

            constexpr void Reset() noexcept
            {
                m_sum = 0;
            }

            [[nodiscard]] constexpr raw_type Raw() const noexcept
            {
                return m_sum;
            }

        private:
            raw_type m_sum {0};
        };

        class Sum8State final : public detail::ByteStateMixin<Sum8State>
        {
        public:
            using value_type = UInt8;
            using detail::ByteStateMixin<Sum8State>::Update;

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                for (UInt8 byte: data)
                    UpdateByte(byte);
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                m_sum = static_cast<value_type>(m_sum + byte);
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                return m_sum;
            }

            constexpr void Reset() noexcept
            {
                m_sum = 0;
            }

            [[nodiscard]] constexpr value_type Raw() const noexcept
            {
                return m_sum;
            }

        private:
            value_type m_sum {0};
        };

        class Sum24State final : public detail::ByteStateMixin<Sum24State>
        {
        public:
            using value_type = UInt32;
            using detail::ByteStateMixin<Sum24State>::Update;

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                for (UInt8 byte: data)
                    UpdateByte(byte);
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                m_sum = static_cast<value_type>((m_sum + byte) & 0x00FFFFFFu);
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                return m_sum;
            }

            constexpr void Reset() noexcept
            {
                m_sum = 0;
            }

            [[nodiscard]] constexpr value_type Raw() const noexcept
            {
                return m_sum;
            }

        private:
            value_type m_sum {0};
        };

        class Sum32State final : public detail::ByteStateMixin<Sum32State>
        {
        public:
            using value_type = UInt32;
            using detail::ByteStateMixin<Sum32State>::Update;

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                for (UInt8 byte: data)
                    UpdateByte(byte);
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                m_sum += byte;
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                return m_sum;
            }

            constexpr void Reset() noexcept
            {
                m_sum = 0;
            }

            [[nodiscard]] constexpr value_type Raw() const noexcept
            {
                return m_sum;
            }

        private:
            value_type m_sum {0};
        };

        class Xor8State final : public detail::ByteStateMixin<Xor8State>
        {
        public:
            using value_type = UInt8;
            using detail::ByteStateMixin<Xor8State>::Update;

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                for (UInt8 byte: data)
                    UpdateByte(byte);
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                m_sum = static_cast<value_type>(m_sum ^ byte);
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                return m_sum;
            }

            constexpr void Reset() noexcept
            {
                m_sum = 0;
            }

            [[nodiscard]] constexpr value_type Raw() const noexcept
            {
                return m_sum;
            }

        private:
            value_type m_sum {0};
        };

        /// @brief Compute BSD checksum (16-bit rotated additive checksum).
        [[nodiscard]] constexpr UInt16 BSDChecksum(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<BSDState>(data);
        }

        [[nodiscard]] constexpr UInt16 BSDChecksum(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<BSDState>(data, len);
        }

        [[nodiscard]] constexpr UInt16 BSDChecksum(std::string_view text) noexcept
        {
            return detail::Compute<BSDState>(text);
        }

        /// @brief Compute SYSV checksum (16-bit folded additive checksum).
        [[nodiscard]] constexpr UInt16 SYSVChecksum(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<SYSVState>(data);
        }

        [[nodiscard]] constexpr UInt16 SYSVChecksum(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<SYSVState>(data, len);
        }

        [[nodiscard]] constexpr UInt16 SYSVChecksum(std::string_view text) noexcept
        {
            return detail::Compute<SYSVState>(text);
        }

        /// @brief Compute sum8 (8-bit additive sum).
        [[nodiscard]] constexpr UInt8 Sum8(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<Sum8State>(data);
        }

        [[nodiscard]] constexpr UInt8 Sum8(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<Sum8State>(data, len);
        }

        [[nodiscard]] constexpr UInt8 Sum8(std::string_view text) noexcept
        {
            return detail::Compute<Sum8State>(text);
        }

        /// @brief Compute sum24 (24-bit additive sum).
        [[nodiscard]] constexpr UInt32 Sum24(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<Sum24State>(data);
        }

        [[nodiscard]] constexpr UInt32 Sum24(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<Sum24State>(data, len);
        }

        [[nodiscard]] constexpr UInt32 Sum24(std::string_view text) noexcept
        {
            return detail::Compute<Sum24State>(text);
        }

        /// @brief Compute sum32 (32-bit additive sum).
        [[nodiscard]] constexpr UInt32 Sum32(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<Sum32State>(data);
        }

        [[nodiscard]] constexpr UInt32 Sum32(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<Sum32State>(data, len);
        }

        [[nodiscard]] constexpr UInt32 Sum32(std::string_view text) noexcept
        {
            return detail::Compute<Sum32State>(text);
        }

        /// @brief Compute xor8 (8-bit xor sum).
        [[nodiscard]] constexpr UInt8 Xor8(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<Xor8State>(data);
        }

        [[nodiscard]] constexpr UInt8 Xor8(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<Xor8State>(data, len);
        }

        [[nodiscard]] constexpr UInt8 Xor8(std::string_view text) noexcept
        {
            return detail::Compute<Xor8State>(text);
        }
    }// namespace Additive

    namespace Internet
    {
        class Checksum16State final : public detail::ByteStateMixin<Checksum16State>
        {
        public:
            using value_type = UInt16;
            using detail::ByteStateMixin<Checksum16State>::Update;

            struct RawState
            {
                UInt32 sum;
                bool   hasOddByte;
                UInt8  oddByte;
            };

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                const auto* bytes = data.data();
                auto        len   = data.size();

                if (m_hasOddByte && len > 0)
                {
                    AddWord(static_cast<UInt16>((static_cast<UInt16>(m_oddByte) << 8) | static_cast<UInt16>(bytes[0])));
                    bytes += 1;
                    len -= 1;
                    m_hasOddByte = false;
                    m_oddByte    = 0;
                }

                while (len >= 2)
                {
                    AddWord(detail::ReadBigEndianUInt16(bytes));
                    bytes += 2;
                    len -= 2;
                }

                if (len == 1)
                {
                    m_hasOddByte = true;
                    m_oddByte    = bytes[0];
                }
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                const UInt8 bytes[] {byte};
                Update(std::span<const UInt8> {bytes, 1});
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                UInt32 sum = m_sum;
                if (m_hasOddByte)
                    sum += static_cast<UInt16>(static_cast<UInt16>(m_oddByte) << 8);

                return static_cast<value_type>(~detail::FoldUInt32ToUInt16(sum));
            }

            constexpr void Reset() noexcept
            {
                m_sum        = 0;
                m_hasOddByte = false;
                m_oddByte    = 0;
            }

            [[nodiscard]] constexpr RawState Raw() const noexcept
            {
                return RawState {m_sum, m_hasOddByte, m_oddByte};
            }

        private:
            constexpr void AddWord(UInt16 word) noexcept
            {
                m_sum += word;
                m_sum = (m_sum & 0xFFFFu) + (m_sum >> 16);
            }

            UInt32 m_sum {0};
            bool   m_hasOddByte {false};
            UInt8  m_oddByte {0};
        };

        /// @brief Compute the 16-bit Internet checksum (one's-complement sum).
        [[nodiscard]] constexpr UInt16 Checksum16(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<Checksum16State>(data);
        }

        [[nodiscard]] constexpr UInt16 Checksum16(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<Checksum16State>(data, len);
        }

        [[nodiscard]] constexpr UInt16 Checksum16(std::string_view text) noexcept
        {
            return detail::Compute<Checksum16State>(text);
        }
    }// namespace Internet

    namespace Adler
    {
        class Adler32State final : public detail::ByteStateMixin<Adler32State>
        {
        public:
            using value_type = UInt32;
            using detail::ByteStateMixin<Adler32State>::Update;

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                std::size_t offset = 0;
                while (offset < data.size())
                {
                    const std::size_t remaining = data.size() - offset;
                    const std::size_t chunk     = remaining > NMax ? NMax : remaining;
                    for (std::size_t i = 0; i < chunk; ++i)
                    {
                        m_a += data[offset + i];
                        m_b += m_a;
                    }

                    m_a %= Modulus;
                    m_b %= Modulus;
                    offset += chunk;
                }
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                m_a = (m_a + byte) % Modulus;
                m_b = (m_b + m_a) % Modulus;
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                return static_cast<value_type>((m_b << 16) | m_a);
            }

            constexpr void Reset() noexcept
            {
                m_a = 1;
                m_b = 0;
            }

            [[nodiscard]] constexpr value_type Raw() const noexcept
            {
                return Finalize();
            }

        private:
            inline static constexpr UInt32      Modulus = 65521u;
            inline static constexpr std::size_t NMax    = 5552;

            UInt32 m_a {1};
            UInt32 m_b {0};
        };

        /// @brief Compute Adler-32 checksum.
        [[nodiscard]] constexpr UInt32 Adler32(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<Adler32State>(data);
        }

        [[nodiscard]] constexpr UInt32 Adler32(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<Adler32State>(data, len);
        }

        [[nodiscard]] constexpr UInt32 Adler32(std::string_view text) noexcept
        {
            return detail::Compute<Adler32State>(text);
        }
    }// namespace Adler

    namespace Fletcher
    {
        class Fletcher16State final : public detail::ByteStateMixin<Fletcher16State>
        {
        public:
            using value_type = UInt16;
            using detail::ByteStateMixin<Fletcher16State>::Update;

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                for (UInt8 byte: data)
                    UpdateByte(byte);
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                m_sum1 = static_cast<UInt16>((m_sum1 + byte) % Modulus);
                m_sum2 = static_cast<UInt16>((m_sum2 + m_sum1) % Modulus);
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                return static_cast<value_type>((m_sum2 << 8) | m_sum1);
            }

            constexpr void Reset() noexcept
            {
                m_sum1 = 0;
                m_sum2 = 0;
            }

            [[nodiscard]] constexpr value_type Raw() const noexcept
            {
                return Finalize();
            }

        private:
            inline static constexpr UInt16 Modulus = 255u;

            UInt16 m_sum1 {0};
            UInt16 m_sum2 {0};
        };

        class Fletcher32State final : public detail::ByteStateMixin<Fletcher32State>
        {
        public:
            using value_type = UInt32;
            using detail::ByteStateMixin<Fletcher32State>::Update;

            struct RawState
            {
                UInt32 sum1;
                UInt32 sum2;
                bool   hasOddByte;
                UInt8  oddByte;
            };

            constexpr void Update(std::span<const UInt8> data) noexcept
            {
                const auto* bytes = data.data();
                auto        len   = data.size();

                if (m_hasOddByte && len > 0)
                {
                    AddWord(static_cast<UInt16>((static_cast<UInt16>(m_oddByte) << 8) | static_cast<UInt16>(bytes[0])));
                    bytes += 1;
                    len -= 1;
                    m_hasOddByte = false;
                    m_oddByte    = 0;
                }

                while (len >= 2)
                {
                    AddWord(detail::ReadBigEndianUInt16(bytes));
                    bytes += 2;
                    len -= 2;
                }

                if (len == 1)
                {
                    m_hasOddByte = true;
                    m_oddByte    = bytes[0];
                }
            }

            constexpr void UpdateByte(UInt8 byte) noexcept
            {
                const UInt8 bytes[] {byte};
                Update(std::span<const UInt8> {bytes, 1});
            }

            [[nodiscard]] constexpr value_type Finalize() const noexcept
            {
                UInt32 sum1 = m_sum1;
                UInt32 sum2 = m_sum2;

                if (m_hasOddByte)
                {
                    sum1 = (sum1 + (static_cast<UInt16>(static_cast<UInt16>(m_oddByte) << 8))) % Modulus;
                    sum2 = (sum2 + sum1) % Modulus;
                }

                return static_cast<value_type>((sum2 << 16) | sum1);
            }

            constexpr void Reset() noexcept
            {
                m_sum1       = 0;
                m_sum2       = 0;
                m_hasOddByte = false;
                m_oddByte    = 0;
            }

            [[nodiscard]] constexpr RawState Raw() const noexcept
            {
                return RawState {m_sum1, m_sum2, m_hasOddByte, m_oddByte};
            }

        private:
            constexpr void AddWord(UInt16 word) noexcept
            {
                m_sum1 = (m_sum1 + word) % Modulus;
                m_sum2 = (m_sum2 + m_sum1) % Modulus;
            }

            inline static constexpr UInt32 Modulus = 65535u;

            UInt32 m_sum1 {0};
            UInt32 m_sum2 {0};
            bool   m_hasOddByte {false};
            UInt8  m_oddByte {0};
        };

        /// @brief Compute the standard Fletcher-16 checksum over octets.
        /// @details The returned checksum packs the accumulators as `(sum2 << 8) | sum1`.
        [[nodiscard]] constexpr UInt16 Fletcher16(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<Fletcher16State>(data);
        }

        [[nodiscard]] constexpr UInt16 Fletcher16(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<Fletcher16State>(data, len);
        }

        [[nodiscard]] constexpr UInt16 Fletcher16(std::string_view text) noexcept
        {
            return detail::Compute<Fletcher16State>(text);
        }

        /// @brief Compute the standard Fletcher-32 checksum over 16-bit big-endian words.
        /// @details A trailing odd byte is zero-padded in the low byte position. The returned checksum packs the accumulators as
        ///          `(sum2 << 16) | sum1`.
        [[nodiscard]] constexpr UInt32 Fletcher32(std::span<const UInt8> data) noexcept
        {
            return detail::Compute<Fletcher32State>(data);
        }

        [[nodiscard]] constexpr UInt32 Fletcher32(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute<Fletcher32State>(data, len);
        }

        [[nodiscard]] constexpr UInt32 Fletcher32(std::string_view text) noexcept
        {
            return detail::Compute<Fletcher32State>(text);
        }
    }// namespace Fletcher

    namespace Decimal
    {
        inline constexpr UInt8 InvalidDigitChecksum = 0xFF;

        namespace detail
        {
            [[nodiscard]] constexpr int DigitValue(char ch) noexcept
            {
                const int digit = ch - '0';
                return (digit >= 0 && digit <= 9) ? digit : -1;
            }

            inline constexpr std::array<std::array<UInt8, 10>, 10> VerhoeffD = {{
                    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}},
                    {{1, 2, 3, 4, 0, 6, 7, 8, 9, 5}},
                    {{2, 3, 4, 0, 1, 7, 8, 9, 5, 6}},
                    {{3, 4, 0, 1, 2, 8, 9, 5, 6, 7}},
                    {{4, 0, 1, 2, 3, 9, 5, 6, 7, 8}},
                    {{5, 9, 8, 7, 6, 0, 4, 3, 2, 1}},
                    {{6, 5, 9, 8, 7, 1, 0, 4, 3, 2}},
                    {{7, 6, 5, 9, 8, 2, 1, 0, 4, 3}},
                    {{8, 7, 6, 5, 9, 3, 2, 1, 0, 4}},
                    {{9, 8, 7, 6, 5, 4, 3, 2, 1, 0}},
            }};

            inline constexpr std::array<std::array<UInt8, 10>, 8> VerhoeffP = {{
                    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}},
                    {{1, 5, 7, 6, 2, 8, 3, 0, 9, 4}},
                    {{5, 8, 0, 3, 7, 9, 6, 1, 4, 2}},
                    {{8, 9, 1, 6, 0, 4, 3, 5, 2, 7}},
                    {{9, 4, 5, 3, 1, 2, 6, 8, 7, 0}},
                    {{4, 2, 8, 6, 5, 7, 3, 9, 0, 1}},
                    {{2, 7, 9, 3, 8, 0, 6, 4, 1, 5}},
                    {{7, 0, 4, 6, 9, 1, 3, 2, 5, 8}},
            }};

            inline constexpr std::array<UInt8, 10> VerhoeffInv {0, 4, 3, 2, 1, 5, 6, 7, 8, 9};

            inline constexpr std::array<std::array<UInt8, 10>, 10> DammTable = {{
                    {{0, 3, 1, 7, 5, 9, 8, 6, 4, 2}},
                    {{7, 0, 9, 2, 1, 5, 4, 8, 6, 3}},
                    {{4, 2, 0, 6, 8, 7, 1, 3, 5, 9}},
                    {{1, 7, 5, 0, 9, 8, 3, 4, 2, 6}},
                    {{6, 1, 2, 3, 0, 4, 5, 9, 7, 8}},
                    {{3, 6, 7, 4, 2, 0, 9, 5, 8, 1}},
                    {{5, 8, 6, 9, 7, 2, 0, 1, 3, 4}},
                    {{8, 9, 4, 5, 3, 6, 2, 0, 1, 7}},
                    {{9, 4, 3, 8, 6, 1, 7, 2, 0, 5}},
                    {{2, 5, 8, 1, 4, 3, 6, 7, 9, 0}},
            }};
        }// namespace detail

        /// @brief Compute the Luhn check digit for a decimal string.
        [[nodiscard]] constexpr UInt8 Luhn(std::string_view digits) noexcept
        {
            int  sum         = 0;
            bool doubleDigit = true;
            for (IntSize i = static_cast<IntSize>(digits.size()) - 1; i >= 0; --i)
            {
                const int digit = detail::DigitValue(digits[static_cast<std::size_t>(i)]);
                if (digit < 0)
                    return InvalidDigitChecksum;

                int value = digit;
                if (doubleDigit)
                {
                    value *= 2;
                    if (value > 9)
                        value -= 9;
                }

                sum += value;
                doubleDigit = !doubleDigit;
            }

            return static_cast<UInt8>((10 - (sum % 10)) % 10);
        }

        [[nodiscard]] constexpr UInt8 Luhn(const char* digits, UIntSize len) noexcept
        {
            if (!digits)
                return len == 0 ? Luhn(std::string_view {}) : InvalidDigitChecksum;
            return Luhn(std::string_view {digits, static_cast<std::size_t>(len)});
        }

        /// @brief Compute the Verhoeff check digit for a decimal string.
        [[nodiscard]] constexpr UInt8 Verhoeff(std::string_view digits) noexcept
        {
            UInt8 c = 0;
            for (UIntSize i = 0; i < digits.size(); ++i)
            {
                const int digit = detail::DigitValue(digits[static_cast<std::size_t>(i)]);
                if (digit < 0)
                    return InvalidDigitChecksum;

                c = detail::VerhoeffD[c][detail::VerhoeffP[(digits.size() - i) % 8][static_cast<std::size_t>(digit)]];
            }

            return detail::VerhoeffInv[c];
        }

        [[nodiscard]] constexpr UInt8 Verhoeff(const char* digits, UIntSize len) noexcept
        {
            if (!digits)
                return len == 0 ? Verhoeff(std::string_view {}) : InvalidDigitChecksum;
            return Verhoeff(std::string_view {digits, static_cast<std::size_t>(len)});
        }

        /// @brief Compute the Damm check digit for a decimal string.
        [[nodiscard]] constexpr UInt8 Damm(std::string_view digits) noexcept
        {
            UInt8 interim = 0;
            for (char ch: digits)
            {
                const int digit = detail::DigitValue(ch);
                if (digit < 0)
                    return InvalidDigitChecksum;

                interim = detail::DammTable[interim][static_cast<std::size_t>(digit)];
            }

            return interim;
        }

        [[nodiscard]] constexpr UInt8 Damm(const char* digits, UIntSize len) noexcept
        {
            if (!digits)
                return len == 0 ? Damm(std::string_view {}) : InvalidDigitChecksum;
            return Damm(std::string_view {digits, static_cast<std::size_t>(len)});
        }
    }// namespace Decimal
}// namespace NGIN::Hashing::Checksum

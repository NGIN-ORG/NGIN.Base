#pragma once

#include <NGIN/Primitives.hpp>
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace NGIN::Math::detail
{
    /// @brief Unsigned binary magnitude used by arbitrary-precision math types.
    class BinaryBigUInt
    {
    public:
        BinaryBigUInt() = default;

        explicit BinaryBigUInt(UInt64 value)
        {
            if (value != 0)
            {
                m_limbs.push_back(static_cast<UInt32>(value));
                const UInt32 high = static_cast<UInt32>(value >> LIMB_BITS);
                if (high != 0)
                    m_limbs.push_back(high);
            }
        }

        [[nodiscard]] bool IsZero() const noexcept { return m_limbs.empty(); }

        [[nodiscard]] UIntSize BitLength() const noexcept
        {
            if (m_limbs.empty())
                return 0;
            return (m_limbs.size() - 1) * LIMB_BITS +
                   (LIMB_BITS - static_cast<UIntSize>(std::countl_zero(m_limbs.back())));
        }

        [[nodiscard]] bool TestBit(UIntSize bit) const noexcept
        {
            const UIntSize limb = bit / LIMB_BITS;
            return limb < m_limbs.size() && (m_limbs[limb] & (UInt32 {1} << (bit % LIMB_BITS))) != 0;
        }

        [[nodiscard]] bool AnyBitsBelow(UIntSize bitCount) const noexcept
        {
            const UIntSize wholeLimbs = std::min(bitCount / LIMB_BITS, m_limbs.size());
            for (UIntSize i = 0; i < wholeLimbs; ++i)
            {
                if (m_limbs[i] != 0)
                    return true;
            }
            const UIntSize remaining = bitCount % LIMB_BITS;
            if (remaining != 0 && wholeLimbs < m_limbs.size())
            {
                const UInt32 mask = (UInt32 {1} << remaining) - 1;
                return (m_limbs[wholeLimbs] & mask) != 0;
            }
            return false;
        }

        [[nodiscard]] int Compare(const BinaryBigUInt& other) const noexcept
        {
            if (m_limbs.size() != other.m_limbs.size())
                return m_limbs.size() < other.m_limbs.size() ? -1 : 1;
            for (UIntSize i = m_limbs.size(); i-- > 0;)
            {
                if (m_limbs[i] != other.m_limbs[i])
                    return m_limbs[i] < other.m_limbs[i] ? -1 : 1;
            }
            return 0;
        }

        void SetBit(UIntSize bit)
        {
            const UIntSize limb = bit / LIMB_BITS;
            if (m_limbs.size() <= limb)
                m_limbs.resize(limb + 1, 0);
            m_limbs[limb] |= UInt32 {1} << (bit % LIMB_BITS);
        }

        void ShiftLeft(UIntSize count)
        {
            if (IsZero() || count == 0)
                return;
            const UIntSize limbShift = count / LIMB_BITS;
            const UIntSize bitShift  = count % LIMB_BITS;
            if (limbShift != 0)
                m_limbs.insert(m_limbs.begin(), limbShift, 0);
            if (bitShift == 0)
                return;

            UInt64 carry = 0;
            for (UInt32& limb: m_limbs)
            {
                const UInt64 shifted = (static_cast<UInt64>(limb) << bitShift) | carry;
                limb                 = static_cast<UInt32>(shifted);
                carry                = shifted >> LIMB_BITS;
            }
            if (carry != 0)
                m_limbs.push_back(static_cast<UInt32>(carry));
        }

        void ShiftRight(UIntSize count)
        {
            if (IsZero() || count == 0)
                return;
            const UIntSize limbShift = count / LIMB_BITS;
            if (limbShift >= m_limbs.size())
            {
                m_limbs.clear();
                return;
            }
            if (limbShift != 0)
                m_limbs.erase(m_limbs.begin(), m_limbs.begin() + static_cast<IntSize>(limbShift));

            const UIntSize bitShift = count % LIMB_BITS;
            if (bitShift != 0)
            {
                UInt32 carry = 0;
                for (UIntSize i = m_limbs.size(); i-- > 0;)
                {
                    const UInt32 nextCarry = static_cast<UInt32>(m_limbs[i] << (LIMB_BITS - bitShift));
                    m_limbs[i]             = static_cast<UInt32>((m_limbs[i] >> bitShift) | carry);
                    carry                  = nextCarry;
                }
            }
            Trim();
        }

        void Add(const BinaryBigUInt& other)
        {
            const UIntSize size = std::max(m_limbs.size(), other.m_limbs.size());
            m_limbs.resize(size, 0);
            UInt64 carry = 0;
            for (UIntSize i = 0; i < size; ++i)
            {
                const UInt64 sum = static_cast<UInt64>(m_limbs[i]) +
                                   (i < other.m_limbs.size() ? other.m_limbs[i] : 0) + carry;
                m_limbs[i] = static_cast<UInt32>(sum);
                carry      = sum >> LIMB_BITS;
            }
            if (carry != 0)
                m_limbs.push_back(static_cast<UInt32>(carry));
        }

        /// @brief Subtracts a magnitude no greater than this one.
        void Subtract(const BinaryBigUInt& other)
        {
            UInt64 borrow = 0;
            for (UIntSize i = 0; i < m_limbs.size(); ++i)
            {
                const UInt64 subtrahend = (i < other.m_limbs.size() ? other.m_limbs[i] : 0) + borrow;
                const UInt64 current    = m_limbs[i];
                m_limbs[i]              = static_cast<UInt32>(current - subtrahend);
                borrow                  = current < subtrahend ? 1 : 0;
            }
            Trim();
        }

        void AddSmall(UInt32 value)
        {
            UInt64 carry = value;
            for (UIntSize i = 0; carry != 0; ++i)
            {
                if (i == m_limbs.size())
                    m_limbs.push_back(0);
                const UInt64 sum = static_cast<UInt64>(m_limbs[i]) + carry;
                m_limbs[i]       = static_cast<UInt32>(sum);
                carry            = sum >> LIMB_BITS;
            }
        }

        void MultiplySmall(UInt32 value)
        {
            if (value == 0 || IsZero())
            {
                m_limbs.clear();
                return;
            }
            UInt64 carry = 0;
            for (UInt32& limb: m_limbs)
            {
                const UInt64 product = static_cast<UInt64>(limb) * value + carry;
                limb                 = static_cast<UInt32>(product);
                carry                = product >> LIMB_BITS;
            }
            if (carry != 0)
                m_limbs.push_back(static_cast<UInt32>(carry));
        }

        [[nodiscard]] UInt32 DivideSmall(UInt32 divisor)
        {
            if (divisor == 0)
                throw std::domain_error("BinaryBigUInt division by zero");
            UInt64 remainder = 0;
            for (UIntSize i = m_limbs.size(); i-- > 0;)
            {
                const UInt64 current = (remainder << LIMB_BITS) | m_limbs[i];
                m_limbs[i]           = static_cast<UInt32>(current / divisor);
                remainder            = current % divisor;
            }
            Trim();
            return static_cast<UInt32>(remainder);
        }

        [[nodiscard]] static BinaryBigUInt Multiply(const BinaryBigUInt& left, const BinaryBigUInt& right)
        {
            if (left.IsZero() || right.IsZero())
                return {};
            BinaryBigUInt result;
            result.m_limbs.assign(left.m_limbs.size() + right.m_limbs.size(), 0);
            for (UIntSize i = 0; i < left.m_limbs.size(); ++i)
            {
                UInt64 carry = 0;
                for (UIntSize j = 0; j < right.m_limbs.size(); ++j)
                {
                    const UInt64 product = static_cast<UInt64>(left.m_limbs[i]) * right.m_limbs[j] +
                                           result.m_limbs[i + j] + carry;
                    result.m_limbs[i + j] = static_cast<UInt32>(product);
                    carry                 = product >> LIMB_BITS;
                }
                UIntSize position = i + right.m_limbs.size();
                while (carry != 0)
                {
                    if (position == result.m_limbs.size())
                        result.m_limbs.push_back(0);
                    const UInt64 sum         = static_cast<UInt64>(result.m_limbs[position]) + carry;
                    result.m_limbs[position] = static_cast<UInt32>(sum);
                    carry                    = sum >> LIMB_BITS;
                    ++position;
                }
            }
            result.Trim();
            return result;
        }

        [[nodiscard]] static std::pair<BinaryBigUInt, BinaryBigUInt>
        DivRem(const BinaryBigUInt& dividend, const BinaryBigUInt& divisor)
        {
            if (divisor.IsZero())
                throw std::domain_error("BinaryBigUInt division by zero");
            if (dividend.Compare(divisor) < 0)
                return {{}, dividend};

            BinaryBigUInt quotient;
            BinaryBigUInt remainder;
            for (UIntSize bit = dividend.BitLength(); bit-- > 0;)
            {
                remainder.ShiftLeft(1);
                if (dividend.TestBit(bit))
                    remainder.AddSmall(1);
                if (remainder.Compare(divisor) >= 0)
                {
                    remainder.Subtract(divisor);
                    quotient.SetBit(bit);
                }
            }
            return {std::move(quotient), std::move(remainder)};
        }

        [[nodiscard]] static BinaryBigUInt PowerSmall(UInt32 base, UIntSize exponent)
        {
            BinaryBigUInt result(1);
            BinaryBigUInt factor(base);
            while (exponent != 0)
            {
                if ((exponent & 1U) != 0)
                    result = Multiply(result, factor);
                exponent >>= 1U;
                if (exponent != 0)
                    factor = Multiply(factor, factor);
            }
            return result;
        }

        [[nodiscard]] static BinaryBigUInt SqrtFloor(const BinaryBigUInt& value, bool* exact = nullptr)
        {
            if (value.IsZero())
            {
                if (exact != nullptr)
                    *exact = true;
                return {};
            }
            BinaryBigUInt current(1);
            current.ShiftLeft((value.BitLength() + 1) / 2);
            for (;;)
            {
                BinaryBigUInt quotient = DivRem(value, current).first;
                BinaryBigUInt next     = current;
                next.Add(quotient);
                next.ShiftRight(1);
                if (next.Compare(current) >= 0)
                    break;
                current = std::move(next);
            }
            if (exact != nullptr)
                *exact = Multiply(current, current).Compare(value) == 0;
            return current;
        }

        [[nodiscard]] UInt64 LowUInt64() const noexcept
        {
            UInt64 value = m_limbs.empty() ? 0 : m_limbs[0];
            if (m_limbs.size() > 1)
                value |= static_cast<UInt64>(m_limbs[1]) << LIMB_BITS;
            return value;
        }

        [[nodiscard]] std::string ToDecimal() const
        {
            if (IsZero())
                return "0";
            BinaryBigUInt       copy = *this;
            std::vector<UInt32> blocks;
            while (!copy.IsZero())
                blocks.push_back(copy.DivideSmall(DECIMAL_BASE));

            std::string result = std::to_string(blocks.back());
            for (UIntSize i = blocks.size() - 1; i-- > 0;)
            {
                std::string block = std::to_string(blocks[i]);
                result.append(DECIMAL_DIGITS - block.size(), '0');
                result += block;
            }
            return result;
        }

        [[nodiscard]] std::string ToHex() const
        {
            if (IsZero())
                return "0";
            constexpr char HEX[] = "0123456789abcdef";
            std::string    result;
            result.reserve((BitLength() + 3) / 4);
            for (UIntSize nibble = (BitLength() + 3) / 4; nibble-- > 0;)
            {
                UInt32 value = 0;
                for (UIntSize bit = 0; bit < 4; ++bit)
                {
                    if (TestBit(nibble * 4 + bit))
                        value |= UInt32 {1} << bit;
                }
                result.push_back(HEX[value]);
            }
            return result;
        }

    private:
        static constexpr UIntSize LIMB_BITS      = 32;
        static constexpr UInt32   DECIMAL_BASE   = 1000000000;
        static constexpr UIntSize DECIMAL_DIGITS = 9;

        void Trim() noexcept
        {
            while (!m_limbs.empty() && m_limbs.back() == 0)
                m_limbs.pop_back();
        }

        std::vector<UInt32> m_limbs;
    };
}// namespace NGIN::Math::detail

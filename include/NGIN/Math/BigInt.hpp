#pragma once
#include <NGIN/Primitives.hpp>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace NGIN::Math
{

    class BigInt
    {

    public:
        /// @brief Divides a value by one base limb and optionally returns the remainder.
        static BigInt
        DivByUInt32(const BigInt& value, UInt32 divisor, UInt32* outRemainder = nullptr)
        {
            if (divisor == 0)
                throw std::runtime_error("Division by zero");

            BigInt result(UninitializedTag {});
            result.m_digits = value.m_digits;
            UInt32 rem      = DivideDigitsByUInt32(result.m_digits, divisor);
            result.Trim();
            result.m_negative = value.m_negative;
            if (outRemainder)
                *outRemainder = rem;
            return result;
        }
        // Construct from std::string
        explicit BigInt(const std::string& str)
            : BigInt(str.c_str()) {}

        // Construct from UInt32 (internal use)
        explicit BigInt(UInt32 value)
        {
            m_negative = false;
            if (value == 0)
            {
                m_digits = std::vector<UInt32> {0};
            }
            else
            {
                m_digits.clear();
                while (value > 0)
                {
                    m_digits.push_back(value % BASE);
                    value /= BASE;
                }
            }
        }

        // Construct from int (internal use)
        explicit BigInt(int value)
        {
            m_negative    = value < 0;
            UInt32 absval = static_cast<UInt32>(m_negative ? -static_cast<Int64>(value) : value);
            if (absval == 0)
            {
                m_digits = {0};
            }
            else
            {
                m_digits.clear();
                while (absval > 0)
                {
                    m_digits.push_back(absval % BASE);
                    absval /= BASE;
                }
            }
        }

        explicit BigInt(UInt64 value)
            : m_negative(false)
        {
            if (value == 0)
            {
                m_digits = {0};
            }
            else
            {
                m_digits.clear();
                while (value > 0)
                {
                    m_digits.push_back(static_cast<UInt32>(value % BASE));
                    value /= BASE;
                }
            }
        }

        // Construct from signed 64‑bit
        explicit BigInt(Int64 value)
        {
            // handle sign (INT64_MIN safely)
            m_negative = (value < 0);
            UInt64 absval;
            if (value < 0)
            {
                // avoid overflow for LLONG_MIN
                absval = static_cast<UInt64>(-(value + 1)) + 1;
            }
            else
            {
                absval = static_cast<UInt64>(value);
            }

            if (absval == 0)
            {
                m_negative = false;
                m_digits   = {0};
            }
            else
            {
                m_digits.clear();
                while (absval > 0)
                {
                    m_digits.push_back(static_cast<UInt32>(absval % BASE));
                    absval /= BASE;
                }
            }
        }

        // Default constructor (zero)
        BigInt()
            : m_digits {0}, m_negative(false)
        {}

        // Construct from C-string (const char*)
        BigInt(const char* str)
        {
            if (!str || !*str)
            {
                m_digits   = {0};
                m_negative = false;
                return;
            }
            m_negative     = (str[0] == '-');
            UIntSize start = m_negative ? 1 : 0;
            m_digits.clear();
            UIntSize len = std::strlen(str);
            m_digits.reserve((len - start + BASE_DIGITS - 1) / BASE_DIGITS);
            for (UIntSize end = len; end > start;)
            {
                const UIntSize begin = end - start > BASE_DIGITS ? end - BASE_DIGITS : start;
                UInt32         block = 0;
                for (UIntSize j = begin; j < end; ++j)
                {
                    block = block * 10 + static_cast<UInt32>(str[j] - '0');
                }
                m_digits.push_back(block);
                end = begin;
            }
            Trim();
        }

        // Addition operator
        BigInt operator+(const BigInt& other) const
        {
            BigInt result(UninitializedTag {});
            if (m_negative == other.m_negative)
            {
                result.m_negative = m_negative;
                result.m_digits   = AddDigits(m_digits, other.m_digits);
                result.Trim();
                return result;
            }

            if (AbsLess(*this, other))
            {
                result.m_negative = other.m_negative;
                result.m_digits   = SubtractDigits(other.m_digits, m_digits);
            }
            else
            {
                result.m_negative = m_negative;
                result.m_digits   = SubtractDigits(m_digits, other.m_digits);
            }
            result.Trim();
            return result;
        }

        // Subtraction operator
        BigInt operator-(const BigInt& other) const
        {
            BigInt result(UninitializedTag {});
            if (m_negative != other.m_negative)
            {
                result.m_negative = m_negative;
                result.m_digits   = AddDigits(m_digits, other.m_digits);
                result.Trim();
                return result;
            }

            if (AbsLess(*this, other))
            {
                result.m_negative = !m_negative;
                result.m_digits   = SubtractDigits(other.m_digits, m_digits);
            }
            else
            {
                result.m_negative = m_negative;
                result.m_digits   = SubtractDigits(m_digits, other.m_digits);
            }
            result.Trim();
            return result;
        }

        // Multiplication operator
        BigInt operator*(const BigInt& other) const
        {
            BigInt result(UninitializedTag {});
            result.m_negative = m_negative != other.m_negative;
            result.m_digits   = MultiplyDigits(m_digits, other.m_digits);
            result.Trim();
            return result;
        }

        // Division operator
        BigInt operator/(const BigInt& other) const
        {
            std::pair<BigInt, BigInt> result = DivRem(other);
            return std::move(result.first);
        }

        // Modulo operator
        BigInt operator%(const BigInt& other) const
        {
            if (other.IsZero())
                throw std::runtime_error("Modulo by zero");
            std::pair<BigInt, BigInt> result = DivRem(other);
            return std::move(result.second);
        }

        /// @brief Divides this value and returns the quotient and remainder from one traversal.
        /// @param divisor Value to divide by; must be non-zero.
        /// @return Quotient truncated toward zero and remainder with the dividend's sign.
        [[nodiscard]] std::pair<BigInt, BigInt> DivRem(const BigInt& divisor) const
        {
            if (divisor.IsZero())
                throw std::runtime_error("Division by zero");

            BigInt quotient(UninitializedTag {});
            BigInt remainder(UninitializedTag {});
            DivMod(*this, divisor, quotient, remainder);
            return {std::move(quotient), std::move(remainder)};
        }

        // Unary minus
        BigInt operator-() const
        {
            BigInt result = *this;
            if (!IsZero())
                result.m_negative = !m_negative;
            else
                result.m_negative = false;
            return result;
        }

        // Comparison operators
        bool operator==(const BigInt& other) const
        {
            return m_negative == other.m_negative && m_digits == other.m_digits;
        }
        bool operator!=(const BigInt& other) const
        {
            return !(*this == other);
        }
        bool operator<(const BigInt& other) const
        {
            if (m_negative != other.m_negative)
                return m_negative;
            if (m_negative)
                return AbsLess(other, *this);
            else
                return AbsLess(*this, other);
        }
        bool operator>(const BigInt& other) const
        {
            return other < *this;
        }
        bool operator<=(const BigInt& other) const
        {
            return !(*this > other);
        }
        bool operator>=(const BigInt& other) const
        {
            return !(*this < other);
        }

        // Output operator
        friend std::ostream& operator<<(std::ostream& os, const BigInt& bi)
        {
            if (bi.m_negative && !bi.IsZero())
                os << '-';
            if (bi.m_digits.empty())
            {
                os << '0';
                return os;
            }
            auto it = bi.m_digits.rbegin();
            os << *it;// Print most significant block without leading zeros
            ++it;
            for (; it != bi.m_digits.rend(); ++it)
            {
                os << std::setw(BASE_DIGITS) << std::setfill('0') << *it;
            }
            return os;
        }

        [[nodiscard]] bool IsZero() const
        {
            return m_digits.size() == 1 && m_digits[0] == 0;
        }

    private:
        struct UninitializedTag
        {
        };

        explicit BigInt(UninitializedTag) noexcept {}

        static constexpr UInt32 BASE        = 1000000000;// 10^9
        static constexpr UInt32 BASE_DIGITS = 9;         // Number of decimal digits per element
        std::vector<UInt32>     m_digits {};             // Least significant digit first
        bool                    m_negative = false;

        static std::vector<UInt32> AddDigits(const std::vector<UInt32>& a, const std::vector<UInt32>& b)
        {
            const UIntSize      n = std::max(a.size(), b.size());
            std::vector<UInt32> result(n + 1, 0);
            UInt64              carry = 0;
            for (UIntSize i = 0; i < n; ++i)
            {
                UInt64 d1  = i < a.size() ? a[i] : 0;
                UInt64 d2  = i < b.size() ? b[i] : 0;
                UInt64 sum = d1 + d2 + carry;
                result[i]  = static_cast<UInt32>(sum % BASE);
                carry      = sum / BASE;
            }
            if (carry)
                result[n] = static_cast<UInt32>(carry);
            else
                result.pop_back();
            return result;
        }

        static std::vector<UInt32> SubtractDigits(const std::vector<UInt32>& a, const std::vector<UInt32>& b)
        {
            std::vector<UInt32> result(a.size(), 0);
            Int64               borrow = 0;
            for (UIntSize i = 0; i < a.size(); ++i)
            {
                Int64 d1  = a[i];
                Int64 d2  = i < b.size() ? b[i] : 0;
                Int64 sub = d1 - d2 - borrow;
                if (sub < 0)
                {
                    sub += BASE;
                    borrow = 1;
                }
                else
                {
                    borrow = 0;
                }
                result[i] = static_cast<UInt32>(sub);
            }
            // Remove leading zeros
            while (result.size() > 1 && result.back() == 0)
                result.pop_back();
            return result;
        }

        static std::vector<UInt32> MultiplyDigitsByUInt32(const std::vector<UInt32>& digits, UInt32 factor)
        {
            if (factor == 0 || (digits.size() == 1 && digits[0] == 0))
                return {0};

            std::vector<UInt32> result(digits.size(), 0);
            UInt64              carry = 0;
            for (UIntSize i = 0; i < digits.size(); ++i)
            {
                const UInt64 product = static_cast<UInt64>(digits[i]) * factor + carry;
                result[i]            = static_cast<UInt32>(product % BASE);
                carry                = product / BASE;
            }
            if (carry)
                result.push_back(static_cast<UInt32>(carry));
            return result;
        }

        static UInt32 DivideDigitsByUInt32(std::vector<UInt32>& digits, UInt32 divisor)
        {
            UInt64 remainder = 0;
            for (UIntSize i = digits.size(); i-- > 0;)
            {
                const UInt64 current = digits[i] + remainder * BASE;
                digits[i]            = static_cast<UInt32>(current / divisor);
                remainder            = current % divisor;
            }
            while (digits.size() > 1 && digits.back() == 0)
                digits.pop_back();
            return static_cast<UInt32>(remainder);
        }

        static std::vector<UInt32> MultiplyDigits(
                const std::vector<UInt32>& a,
                const std::vector<UInt32>& b)
        {
            // Zero‐short circuit
            if ((a.size() == 1 && a[0] == 0) ||
                (b.size() == 1 && b[0] == 0))
                return {0};

            // Use grade–school up through 31‑limb numbers
            if (a.size() < 32 || b.size() < 32)
            {
                std::vector<UInt32> result(a.size() + b.size(), 0);
                for (size_t i = 0; i < a.size(); ++i)
                {
                    UInt64 carry = 0;
                    for (size_t j = 0; j < b.size() || carry; ++j)
                    {
                        UInt64 cur    = result[i + j] + UInt64(a[i]) * (j < b.size() ? UInt64(b[j]) : 0) + carry;
                        result[i + j] = UInt32(cur % BASE);
                        carry         = cur / BASE;
                    }
                }
                // strip leading zeros
                while (result.size() > 1 && result.back() == 0)
                    result.pop_back();
                return result;
            }

            // Karatsuba for larger sizes
            size_t n    = std::max(a.size(), b.size());
            size_t half = n / 2;

            // split each operand at `half`
            auto split = [&](const std::vector<UInt32>& v) {
                size_t     cut    = std::min(half, v.size());
                const auto offset = static_cast<std::ptrdiff_t>(cut);
                return std::pair<
                        std::vector<UInt32>,
                        std::vector<UInt32>> {
                        std::vector<UInt32>(v.begin(), v.begin() + offset),
                        std::vector<UInt32>(v.begin() + offset, v.end())};
            };
            auto [a_low, a_high] = split(a);
            auto [b_low, b_high] = split(b);

            auto z0 = MultiplyDigits(a_low, b_low);
            auto z2 = MultiplyDigits(a_high, b_high);

            // (a_low + a_high) * (b_low + b_high)
            auto a_sum = AddDigits(a_low, a_high);
            auto b_sum = AddDigits(b_low, b_high);
            auto z1    = MultiplyDigits(a_sum, b_sum);
            // subtract out z0 and z2 to get the cross terms
            z1 = SubtractDigits(z1, z0);
            z1 = SubtractDigits(z1, z2);

            // allocate full result = a.size()+b.size()
            std::vector<UInt32> result(a.size() + b.size(), 0);

            // add z0 at offset 0
            for (size_t i = 0; i < z0.size(); ++i)
                result[i] += z0[i];

            // add z1 at offset = half
            for (size_t i = 0; i < z1.size(); ++i)
                result[i + half] += z1[i];

            // add z2 at offset = 2*half
            for (size_t i = 0; i < z2.size(); ++i)
                result[i + 2 * half] += z2[i];

            // propagate carries
            UInt64 carry = 0;
            for (size_t i = 0; i < result.size(); ++i)
            {
                UInt64 cur = UInt64(result[i]) + carry;
                result[i]  = UInt32(cur % BASE);
                carry      = cur / BASE;
            }

            // strip leading zeros
            while (result.size() > 1 && result.back() == 0)
                result.pop_back();

            return result;
        }


        static void DivMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r)
        {
            if (b.IsZero())
                throw std::runtime_error("Division by zero");
            if (a.IsZero())
            {
                q = BigInt(0);
                r = BigInt(0);
                return;
            }
            if (AbsLess(a, b))
            {
                q = BigInt(0);
                r = a;
                return;
            }
            if (b.m_digits.size() == 1)
            {
                DivModBySingleLimb(a, b.m_digits[0], q, r);
            }
            else
            {
                KnuthDivMod(a, b, q, r);
            }
            q.m_negative = (a.m_negative != b.m_negative) && !q.IsZero();
            r.m_negative = a.m_negative && !r.IsZero();
        }

        // --- Division helpers ---
        static void DivModBySingleLimb(const BigInt& a, UInt32 b, BigInt& q, BigInt& r)
        {
            UInt32 rem   = 0;
            q            = DivByUInt32(a, b, &rem);
            q.m_negative = false;
            r            = BigInt(rem);
        }

        static void KnuthDivMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r)
        {
            const UIntSize divisorSize   = b.m_digits.size();
            const UIntSize quotientSize  = a.m_digits.size() - divisorSize + 1;
            const UInt32   normalization = BASE / (b.m_digits.back() + 1);

            std::vector<UInt32> dividend = MultiplyDigitsByUInt32(a.m_digits, normalization);
            std::vector<UInt32> divisor  = MultiplyDigitsByUInt32(b.m_digits, normalization);
            dividend.resize(a.m_digits.size() + 1, 0);

            q.m_digits.assign(quotientSize, 0);
            q.m_negative = false;

            for (UIntSize position = quotientSize; position-- > 0;)
            {
                const UInt64 numerator =
                        static_cast<UInt64>(dividend[position + divisorSize]) * BASE +
                        dividend[position + divisorSize - 1];
                UInt64 quotientDigit     = numerator / divisor.back();
                UInt64 estimateRemainder = numerator % divisor.back();

                if (quotientDigit == BASE)
                {
                    --quotientDigit;
                    estimateRemainder += divisor.back();
                }
                while (estimateRemainder < BASE &&
                       quotientDigit * divisor[divisorSize - 2] >
                               estimateRemainder * BASE + dividend[position + divisorSize - 2])
                {
                    --quotientDigit;
                    estimateRemainder += divisor.back();
                }

                UInt64 borrow = 0;
                for (UIntSize i = 0; i < divisorSize; ++i)
                {
                    const UInt64 product = quotientDigit * divisor[i] + borrow;
                    const UInt32 low     = static_cast<UInt32>(product % BASE);
                    borrow               = product / BASE;
                    if (dividend[position + i] < low)
                    {
                        dividend[position + i] =
                                static_cast<UInt32>(dividend[position + i] + BASE - low);
                        ++borrow;
                    }
                    else
                    {
                        dividend[position + i] -= low;
                    }
                }

                const bool overestimated         = dividend[position + divisorSize] < borrow;
                dividend[position + divisorSize] = static_cast<UInt32>(
                        dividend[position + divisorSize] + (overestimated ? BASE : 0) - borrow);
                if (overestimated)
                {
                    --quotientDigit;
                    UInt64 carry = 0;
                    for (UIntSize i = 0; i < divisorSize; ++i)
                    {
                        const UInt64 sum =
                                static_cast<UInt64>(dividend[position + i]) + divisor[i] + carry;
                        if (sum >= BASE)
                        {
                            dividend[position + i] = static_cast<UInt32>(sum - BASE);
                            carry                  = 1;
                        }
                        else
                        {
                            dividend[position + i] = static_cast<UInt32>(sum);
                            carry                  = 0;
                        }
                    }
                    dividend[position + divisorSize] = static_cast<UInt32>(
                            (dividend[position + divisorSize] + carry) % BASE);
                }
                q.m_digits[position] = static_cast<UInt32>(quotientDigit);
            }

            q.Trim();
            r.m_digits.assign(
                    dividend.begin(),
                    dividend.begin() + static_cast<std::ptrdiff_t>(divisorSize));
            r.m_negative = false;
            if (normalization != 1)
                DivideDigitsByUInt32(r.m_digits, normalization);
            r.Trim();
        }


        static bool AbsLess(const BigInt& a, const BigInt& b)
        {
            if (a.m_digits.size() != b.m_digits.size())
                return a.m_digits.size() < b.m_digits.size();
            for (UIntSize i = a.m_digits.size(); i-- > 0;)
            {
                if (a.m_digits[i] != b.m_digits[i])
                    return a.m_digits[i] < b.m_digits[i];
            }
            return false;
        }

        void Trim()
        {
            if (m_digits.empty())
                m_digits.push_back(0);
            while (m_digits.size() > 1 && m_digits.back() == 0)
                m_digits.pop_back();
            if (IsZero())
                m_negative = false;
        }
    };
}// namespace NGIN::Math

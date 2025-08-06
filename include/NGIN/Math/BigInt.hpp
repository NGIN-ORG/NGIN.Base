#pragma once
#include <NGIN/Primitives.hpp>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <iomanip>
namespace NGIN::Math
{

    class BigInt
    {

    public:
        // Helper: divide BigInt by UInt32 (for unnormalizing remainder)
        static BigInt
        DivByUInt32(const BigInt& value, UInt32 divisor, UInt32* outRemainder = nullptr)
        {
            BigInt result;
            result.m_digits.resize(value.m_digits.size());
            UInt64 rem = 0;
            for (Int64 i = value.m_digits.size() - 1; i >= 0; --i)
            {
                UInt64 cur         = value.m_digits[i] + rem * BASE;
                result.m_digits[i] = static_cast<UInt32>(cur / divisor);
                rem                = cur % divisor;
            }
            result.Trim();
            result.m_negative = value.m_negative;
            if (outRemainder)
                *outRemainder = static_cast<UInt32>(rem);
            return result;
        }
        // Construct from std::string
        explicit BigInt(const std::string& str) : BigInt(str.c_str()) {}

        // Construct from UInt32 (internal use)
        explicit BigInt(UInt32 value)
        {
            m_negative = false;
            if (value == 0)
            {
                m_digits = {0};
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
        BigInt() : m_digits {0}, m_negative(false)
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
            std::vector<UInt32> temp_digits;
            for (Int64 i = len; i > static_cast<Int64>(start); i -= BASE_DIGITS)
            {
                UInt32 block = 0;
                for (Int64 j = std::max(static_cast<Int64>(start), i - BASE_DIGITS); j < i; ++j)
                {
                    block = block * 10 + (str[j] - '0');
                }
                temp_digits.push_back(block);
            }
            m_digits = temp_digits;
            Trim();
            // Normalize zero: always non-negative
            if (IsZero())
                m_negative = false;
        }

        // Addition operator
        BigInt operator+(const BigInt& other) const
        {
            if (m_negative == other.m_negative)
            {
                BigInt result;
                result.m_negative = m_negative;
                result.m_digits   = AddDigits(m_digits, other.m_digits);
                result.Trim();
                return result;
            }
            // Mixed sign: a + (-b) == a - b, (-a) + b == b - a
            if (m_negative)
                return other - (-*this);
            else
                return *this - (-other);
        }

        // Subtraction operator
        BigInt operator-(const BigInt& other) const
        {
            if (m_negative == other.m_negative)
            {
                if (AbsLess(*this, other))
                {
                    BigInt result;
                    result.m_negative = !m_negative;
                    result.m_digits   = SubtractDigits(other.m_digits, m_digits);
                    result.Trim();
                    return result;
                }
                else
                {
                    BigInt result;
                    result.m_negative = m_negative;
                    result.m_digits   = SubtractDigits(m_digits, other.m_digits);
                    result.Trim();
                    return result;
                }
            }
            // Mixed sign: a - (-b) == a + b, (-a) - b == -(a + b)
            if (m_negative)
                return -((-*this) + other);
            else
                return *this + (-other);
        }

        // Multiplication operator
        BigInt operator*(const BigInt& other) const
        {
            BigInt result;
            result.m_negative = m_negative != other.m_negative;
            result.m_digits   = MultiplyDigits(m_digits, other.m_digits);
            result.Trim();
            return result;
        }

        // Division operator
        BigInt operator/(const BigInt& other) const
        {
            if (other.IsZero())
                throw std::runtime_error("Division by zero");
            BigInt quotient, remainder;
            DivMod(*this, other, quotient, remainder);
            // Truncate towards zero (C++/Python semantics)
            return quotient;
        }

        // Modulo operator
        BigInt operator%(const BigInt& other) const
        {
            if (other.IsZero())
                throw std::runtime_error("Modulo by zero");
            BigInt quotient, remainder;
            DivMod(*this, other, quotient, remainder);
            // Set remainder sign to match dividend (C++/Python semantics)
            if (m_negative && !remainder.IsZero())
                remainder = -remainder;
            return remainder;
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
        static constexpr UInt32 BASE        = 1000000000;// 10^9
        static constexpr UInt32 BASE_DIGITS = 9;         // Number of decimal digits per element
        std::vector<UInt32> m_digits {};                 // Least significant digit first
        bool m_negative = false;

        static std::vector<UInt32> AddDigits(const std::vector<UInt32>& a, const std::vector<UInt32>& b)
        {
            std::vector<UInt32> result;
            result.reserve(std::max(a.size(), b.size()) + 1);
            UIntSize n   = std::max(a.size(), b.size());
            UInt64 carry = 0;
            for (UIntSize i = 0; i < n || carry; ++i)
            {
                UInt64 d1  = i < a.size() ? a[i] : 0;
                UInt64 d2  = i < b.size() ? b[i] : 0;
                UInt64 sum = d1 + d2 + carry;
                result.push_back(static_cast<UInt32>(sum % BASE));
                carry = sum / BASE;
            }
            return result;
        }

        static std::vector<UInt32> SubtractDigits(const std::vector<UInt32>& a, const std::vector<UInt32>& b)
        {
            std::vector<UInt32> result;
            result.reserve(a.size());
            Int64 borrow = 0;
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
                result.push_back(static_cast<UInt32>(sub));
            }
            // Remove leading zeros
            while (result.size() > 1 && result.back() == 0)
                result.pop_back();
            return result;
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
                size_t cut = std::min(half, v.size());
                return std::pair<
                        std::vector<UInt32>,
                        std::vector<UInt32>> {
                        std::vector<UInt32>(v.begin(), v.begin() + cut),
                        std::vector<UInt32>(v.begin() + cut, v.end())};
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
            size_t n = a.m_digits.size();
            size_t m = b.m_digits.size();
            if (b.IsZero())
                throw std::runtime_error("Division by zero");
            if (a.IsZero())
            {
                q = BigInt(0);
                r = BigInt(0);
                return;
            }
            if (AbsLess(a.Abs(), b.Abs()))
            {
                q = BigInt(0);
                r = a;
                return;
            }
            if (m == 1)
            {
                DivModBySingleLimb(a, b.m_digits[0], q, r);
            }
            else if (n <= 4 && m <= 4)
            {
                NaiveDivMod(a, b, q, r);
            }
            else if (n < 256 && m < 256)
            {
                KnuthDivMod(a, b, q, r);
            }
            else
            {
                // Burnikel–Ziegler or Newton–Raphson for huge numbers

                FastDivMod(a, b, q, r);
            }
            // Set quotient sign
            q.m_negative = (a.m_negative != b.m_negative) && !q.IsZero();
            // Set remainder sign to match dividend
            // if (a.m_negative && !r.IsZero())
            //     r = -r;
        }

        // --- Division helpers ---
        static void DivModBySingleLimb(const BigInt& a, UInt32 b, BigInt& q, BigInt& r)
        {
            // Fast path: divide a by a single-limb b
            q          = DivByUInt32(a.Abs(), b, nullptr);
            UInt32 rem = 0;
            DivByUInt32(a.Abs(), b, &rem);
            r = BigInt(rem);
        }

        static void NaiveDivMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r)
        {
            // Schoolbook division for tiny numbers
            BigInt dividend = a.Abs();
            BigInt divisor  = b.Abs();
            q               = BigInt(0);
            r               = dividend;
            while (!AbsLess(r, divisor))
            {
                r = r - divisor;
                q = q + BigInt(1);
            }
        }

        static void KnuthDivMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r)
        {
            using UInt      = UInt32;
            BigInt dividend = a.Abs();
            BigInt divisor  = b.Abs();
            std::size_t n   = dividend.m_digits.size();
            q.m_digits.assign(n, 0);
            BigInt rem(0);
            for (int i = int(n) - 1; i >= 0; --i)
            {
                rem.m_digits.insert(rem.m_digits.begin(), dividend.m_digits[i]);
                rem.Trim();
                UInt low = 0, high = BASE - 1, qd = 0;
                while (low <= high)
                {
                    UInt mid    = low + ((high - low) >> 1);
                    BigInt prod = divisor * BigInt(mid);
                    if (prod <= rem)
                    {
                        qd  = mid;
                        low = mid + 1;
                    }
                    else
                    {
                        high = mid - 1;
                    }
                }
                q.m_digits[i] = qd;
                if (qd)
                {
                    BigInt prod = divisor * BigInt(qd);
                    rem         = rem - prod;
                }
            }
            q.Trim();
            r = rem;
        }

        static void FastDivMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r)
        {
            // Burnikel–Ziegler division for huge numbers (blockwise recursive)
            // This is a simplified version for demonstration, not fully optimized
            // Reference: https://hal.inria.fr/inria-00072854/document
            const size_t n = a.m_digits.size();
            const size_t m = b.m_digits.size();
            if (m == 0 || b.IsZero())
                throw std::runtime_error("Division by zero");
            if (n < m || a.IsZero())
            {
                q = BigInt(0);
                r = a;
                return;
            }
            // For small cases, fallback to Knuth
            if (n < 512 || m < 32)
            {
                KnuthDivMod(a, b, q, r);
                return;
            }

            // Choose block size k (must be >= m/2)
            size_t k = (m + 1) / 2;
            size_t s = (n + k - 1) / k;// number of blocks in a
            // Split a and b into blocks of k limbs
            auto split_blocks = [](const BigInt& x, size_t blocksize) -> std::vector<BigInt> {
                std::vector<BigInt> blocks;
                size_t total = x.m_digits.size();
                for (size_t i = 0; i < total; i += blocksize)
                {
                    std::vector<UInt32> part;
                    for (size_t j = i; j < std::min(i + blocksize, total); ++j)
                        part.push_back(x.m_digits[j]);
                    BigInt bpart;
                    bpart.m_digits   = part;
                    bpart.m_negative = false;
                    bpart.Trim();
                    blocks.push_back(bpart);
                }
                return blocks;
            };

            // Helper: shift left by k limbs (multiply by BASE^k)
            auto shift_left_limbs = [](const BigInt& x, size_t limbs) -> BigInt {
                if (x.IsZero())
                    return x;
                BigInt res = x;
                res.m_digits.insert(res.m_digits.begin(), limbs, 0);
                return res;
            };

            // Split a and b
            std::vector<BigInt> a_blocks = split_blocks(a.Abs(), k);
            std::vector<BigInt> b_blocks = split_blocks(b.Abs(), k);

            // Compose b_hat = b shifted to align with a's highest block
            size_t t     = a_blocks.size() - b_blocks.size();
            BigInt b_hat = shift_left_limbs(b.Abs(), t * k);

            BigInt rem = a.Abs();
            q          = BigInt(0);
            for (size_t i = t + 1; i-- > 0;)
            {
                // Estimate quotient digit for this block
                BigInt qhat, rhat;
                KnuthDivMod(rem, b_hat, qhat, rhat);
                // q = q * BASE^{k} + qhat
                q = shift_left_limbs(q, k);
                q = q + qhat;
                // rem = rhat
                rem = rhat;
                // Shift b_hat right by k limbs for next block
                if (b_hat.m_digits.size() > k)
                    b_hat.m_digits.erase(b_hat.m_digits.begin(), b_hat.m_digits.begin() + k);
                else
                    b_hat = BigInt(0);
                b_hat.Trim();
            }
            q.Trim();
            r = rem;
        }


        static bool AbsLess(const BigInt& a, const BigInt& b)
        {
            if (a.m_digits.size() != b.m_digits.size())
                return a.m_digits.size() < b.m_digits.size();
            for (Int64 i = a.m_digits.size() - 1; i >= 0; --i)
            {
                if (a.m_digits[i] != b.m_digits[i])
                    return a.m_digits[i] < b.m_digits[i];
            }
            return false;
        }

        [[nodiscard]] BigInt Abs() const
        {
            BigInt result     = *this;
            result.m_negative = false;
            return result;
        }

        void Trim()
        {
            while (m_digits.size() > 1 && m_digits.back() == 0)
                m_digits.pop_back();
        }
    };
}// namespace NGIN::Math
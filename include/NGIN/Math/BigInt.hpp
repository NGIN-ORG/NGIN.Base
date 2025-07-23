#pragma once
#include <NGIN/Primitives.hpp>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace NGIN::Math
{

    class BigInt
    {
    public:
        // Default constructor (zero)
        BigInt() : m_digits {0}, m_negative(false) {}

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
            for (UIntSize i = len; i > start; --i)
            {
                char c = str[i - 1];
                if (c >= '0' && c <= '9')
                    m_digits.push_back(c - '0');
            }
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
            return quotient;
        }

        // Modulo operator
        BigInt operator%(const BigInt& other) const
        {
            if (other.IsZero())
                throw std::runtime_error("Modulo by zero");
            BigInt quotient, remainder;
            DivMod(*this, other, quotient, remainder);
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
            for (auto it = bi.m_digits.rbegin(); it != bi.m_digits.rend(); ++it)
                os << char('0' + *it);
            return os;
        }

        [[nodiscard]] bool IsZero() const
        {
            return m_digits.size() == 1 && m_digits[0] == 0;
        }

    private:
        std::vector<UInt8> m_digits {};// Least significant digit first
        bool m_negative = false;

        static std::vector<UInt8> AddDigits(const std::vector<UInt8>& a, const std::vector<UInt8>& b)
        {
            std::vector<UInt8> result;
            result.reserve(std::max(a.size(), b.size()) + 1);
            UIntSize n  = std::max(a.size(), b.size());
            UInt8 carry = 0;
            for (UIntSize i = 0; i < n || carry; ++i)
            {
                UInt8 d1  = i < a.size() ? a[i] : 0;
                UInt8 d2  = i < b.size() ? b[i] : 0;
                UInt8 sum = d1 + d2 + carry;
                result.push_back(sum % 10);
                carry = sum / 10;
            }
            return result;
        }

        static std::vector<UInt8> SubtractDigits(const std::vector<UInt8>& a, const std::vector<UInt8>& b)
        {
            std::vector<UInt8> result;
            result.reserve(a.size());
            UInt8 borrow = 0;
            for (UIntSize i = 0; i < a.size(); ++i)
            {
                Int16 d1  = a[i];
                Int16 d2  = i < b.size() ? b[i] : 0;
                Int16 sub = d1 - d2 - borrow;
                if (sub < 0)
                {
                    sub += 10;
                    borrow = 1;
                }
                else
                {
                    borrow = 0;
                }
                result.push_back(static_cast<UInt8>(sub));
            }
            // Remove leading zeros
            while (result.size() > 1 && result.back() == 0)
                result.pop_back();
            return result;
        }

        static std::vector<UInt8> MultiplyDigits(const std::vector<UInt8>& a, const std::vector<UInt8>& b)
        {
            if ((a.size() == 1 && a[0] == 0) || (b.size() == 1 && b[0] == 0))
                return {0};
            std::vector<UInt8> result(a.size() + b.size(), 0);
            for (UIntSize i = 0; i < a.size(); ++i)
            {
                UInt8 carry = 0;
                for (UIntSize j = 0; j < b.size() || carry; ++j)
                {
                    UInt64 cur    = result[i + j] + a[i] * (j < b.size() ? b[j] : 0) + carry;
                    result[i + j] = cur % 10;
                    carry         = cur / 10;
                }
            }
            // Remove leading zeros
            while (result.size() > 1 && result.back() == 0)
                result.pop_back();
            return result;
        }

        static void DivMod(const BigInt& dividend, const BigInt& divisor, BigInt& quotient, BigInt& remainder)
        {
            if (divisor.IsZero())
                throw std::runtime_error("Division by zero");
            const BigInt a = dividend.Abs();
            const BigInt b = divisor.Abs();
            quotient       = BigInt();
            remainder      = BigInt();
            remainder.m_digits.clear();
            remainder.m_negative = false;
            if (b.IsZero())
                throw std::runtime_error("Division by zero");
            Int64 n = static_cast<Int64>(a.m_digits.size());
            for (Int64 i = n - 1; i >= 0; --i)
            {
                remainder.m_digits.insert(remainder.m_digits.begin(), a.m_digits[i]);
                remainder.Trim();
                UInt8 x = 0;
                if (!AbsLess(remainder, b))
                {
                    UInt8 left = 1, right = 9;
                    while (left <= right)
                    {
                        UInt8 mid   = (left + right) / 2;
                        BigInt prod = b * BigInt(std::to_string(mid).c_str());
                        if (!AbsLess(remainder, prod))
                        {
                            x    = mid;
                            left = mid + 1;
                        }
                        else
                        {
                            right = mid - 1;
                        }
                    }
                    remainder = remainder - b * BigInt(std::to_string(x).c_str());
                }
                quotient.m_digits.insert(quotient.m_digits.begin(), x);
            }
            quotient.Trim();
            remainder.Trim();
            quotient.m_negative  = dividend.m_negative != divisor.m_negative && !quotient.IsZero();
            remainder.m_negative = dividend.m_negative && !remainder.IsZero();
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
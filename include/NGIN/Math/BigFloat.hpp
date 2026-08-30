#pragma once

/// @file BigFloat.hpp
/// @brief Deterministic fixed-precision arbitrary-precision binary floating point.

#include <NGIN/Math/detail/BinaryBigUInt.hpp>
#include <NGIN/Primitives.hpp>
#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace NGIN::Math
{
    /// @brief Rounding policy applied after every BigFloat operation.
    enum class BigFloatRoundingMode : UInt8
    {
        ToNearestEven,
        TowardZero,
        TowardPositive,
        TowardNegative,
        AwayFromZero,
    };

    /// @brief Classification of a BigFloat value.
    enum class BigFloatClass : UInt8
    {
        Zero,
        Finite,
        Infinity,
        NaN,
    };

    /// @brief Binary floating-point value with a compile-time significand precision.
    /// @tparam PrecisionBits Number of significand bits, including the leading bit.
    /// @tparam RoundingMode Deterministic rounding policy used by arithmetic operations.
    ///
    /// Finite non-zero values are represented as `significand * 2^(exponent-(PrecisionBits-1))`.
    /// The normalized exponent range is [-1,000,000, +1,000,000]. The type also has signed zero,
    /// infinity, and quiet NaN. It does not use a mutable global floating-point environment and is
    /// therefore suitable for reproducible work.
    template<UIntSize PrecisionBits, BigFloatRoundingMode RoundingMode = BigFloatRoundingMode::ToNearestEven>
    class BigFloat
    {
        static_assert(PrecisionBits >= 2, "BigFloat requires at least two bits of precision");

        template<UIntSize, BigFloatRoundingMode>
        friend class BigFloat;

    public:
        static constexpr UIntSize             PRECISION_BITS = PrecisionBits;
        static constexpr BigFloatRoundingMode ROUNDING_MODE  = RoundingMode;

        BigFloat() noexcept = default;

        template<std::integral T>
            requires(!std::same_as<std::remove_cv_t<T>, bool>)
        BigFloat(T value)
        {
            AssignIntegral(value);
        }

        BigFloat(F32 value) { AssignFloat(value); }
        BigFloat(F64 value) { AssignFloat(value); }

        explicit BigFloat(std::string_view text) { *this = Parse(text); }
        explicit BigFloat(const std::string& text)
            : BigFloat(std::string_view(text))
        {}
        explicit BigFloat(const char* text)
            : BigFloat(CheckedStringView(text))
        {}

        template<UIntSize OtherPrecision, BigFloatRoundingMode OtherMode>
        explicit BigFloat(const BigFloat<OtherPrecision, OtherMode>& other)
        {
            m_class    = other.m_class;
            m_negative = other.m_negative;
            m_exponent = other.m_exponent;
            if (other.m_class == BigFloatClass::Finite)
            {
                *this = RoundFinite(
                        other.m_significand,
                        CheckedSubtract(other.m_exponent, static_cast<Int64>(OtherPrecision - 1)),
                        other.m_negative,
                        false);
            }
        }

        /// @brief Parses a decimal or scientific-notation value, `inf`, or `nan`.
        /// @throws std::invalid_argument for malformed input.
        /// @throws std::out_of_range when a decimal exponent is impractically large to convert.
        [[nodiscard]] static BigFloat Parse(std::string_view text)
        {
            if (text.empty())
                throw std::invalid_argument("BigFloat cannot parse an empty string");

            bool negative = false;
            if (text.front() == '+' || text.front() == '-')
            {
                negative = text.front() == '-';
                text.remove_prefix(1);
            }
            if (text.empty())
                throw std::invalid_argument("BigFloat sign is not a number");

            const std::string lower = Lowercase(text);
            if (lower == "nan")
                return QuietNaN(negative);
            if (lower == "inf" || lower == "infinity")
                return Infinity(negative);
            if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
                return ParseHex(text, negative);

            detail::BinaryBigUInt digits;
            UIntSize              fractionalDigits = 0;
            bool                  sawDigit         = false;
            bool                  sawPoint         = false;
            UIntSize              position         = 0;
            for (; position < text.size(); ++position)
            {
                const char character = text[position];
                if (character >= '0' && character <= '9')
                {
                    sawDigit = true;
                    digits.MultiplySmall(10);
                    digits.AddSmall(static_cast<UInt32>(character - '0'));
                    if (sawPoint)
                        ++fractionalDigits;
                    continue;
                }
                if (character == '.' && !sawPoint)
                {
                    sawPoint = true;
                    continue;
                }
                break;
            }
            if (!sawDigit)
                throw std::invalid_argument("BigFloat requires at least one decimal digit");

            Int64 explicitExponent = 0;
            if (position < text.size() && (text[position] == 'e' || text[position] == 'E'))
            {
                ++position;
                bool exponentNegative = false;
                if (position < text.size() && (text[position] == '+' || text[position] == '-'))
                {
                    exponentNegative = text[position] == '-';
                    ++position;
                }
                if (position == text.size() || !std::isdigit(static_cast<unsigned char>(text[position])))
                    throw std::invalid_argument("BigFloat has an invalid decimal exponent");
                for (; position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])); ++position)
                {
                    const Int32 digit = text[position] - '0';
                    if (explicitExponent > (MAX_DECIMAL_CONVERSION_EXPONENT - digit) / 10)
                        throw std::out_of_range("BigFloat decimal exponent is too large");
                    explicitExponent = explicitExponent * 10 + digit;
                }
                if (exponentNegative)
                    explicitExponent = -explicitExponent;
            }
            if (position != text.size())
                throw std::invalid_argument("BigFloat contains trailing characters");
            if (digits.IsZero())
                return Zero(negative);
            if (fractionalDigits > static_cast<UIntSize>(MAX_DECIMAL_CONVERSION_EXPONENT))
                throw std::out_of_range("BigFloat has too many fractional digits");

            const Int64 decimalExponent = explicitExponent - static_cast<Int64>(fractionalDigits);
            if (decimalExponent > MAX_DECIMAL_CONVERSION_EXPONENT ||
                decimalExponent < -MAX_DECIMAL_CONVERSION_EXPONENT)
                throw std::out_of_range("BigFloat decimal exponent is too large");

            detail::BinaryBigUInt numerator = std::move(digits);
            detail::BinaryBigUInt denominator(1);
            if (decimalExponent > 0)
            {
                numerator = detail::BinaryBigUInt::Multiply(
                        numerator,
                        detail::BinaryBigUInt::PowerSmall(5, static_cast<UIntSize>(decimalExponent)));
            }
            else if (decimalExponent < 0)
            {
                denominator = detail::BinaryBigUInt::PowerSmall(5, static_cast<UIntSize>(-decimalExponent));
            }
            return RoundRational(numerator, denominator, decimalExponent, negative);
        }

        [[nodiscard]] static BigFloat Zero(bool negative = false) noexcept
        {
            BigFloat result;
            result.m_negative = negative;
            return result;
        }

        [[nodiscard]] static BigFloat Infinity(bool negative = false) noexcept
        {
            BigFloat result;
            result.m_class    = BigFloatClass::Infinity;
            result.m_negative = negative;
            return result;
        }

        [[nodiscard]] static BigFloat QuietNaN(bool negative = false) noexcept
        {
            BigFloat result;
            result.m_class    = BigFloatClass::NaN;
            result.m_negative = negative;
            return result;
        }

        /// @brief Smallest positive normalized value supported by the exponent domain.
        [[nodiscard]] static BigFloat MinPositive() { return MinimumPositive(); }

        /// @brief Largest finite value supported by the exponent domain.
        [[nodiscard]] static BigFloat MaxFinite(bool negative = false) { return MaximumFinite(negative); }

        [[nodiscard]] BigFloatClass Classify() const noexcept { return m_class; }
        [[nodiscard]] bool          IsZero() const noexcept { return m_class == BigFloatClass::Zero; }
        [[nodiscard]] bool          IsFinite() const noexcept
        {
            return m_class == BigFloatClass::Zero || m_class == BigFloatClass::Finite;
        }
        [[nodiscard]] bool  IsInfinity() const noexcept { return m_class == BigFloatClass::Infinity; }
        [[nodiscard]] bool  IsNaN() const noexcept { return m_class == BigFloatClass::NaN; }
        [[nodiscard]] bool  SignBit() const noexcept { return m_negative; }
        [[nodiscard]] Int64 Exponent() const noexcept { return m_exponent; }

        [[nodiscard]] BigFloat Abs() const noexcept
        {
            BigFloat result   = *this;
            result.m_negative = false;
            return result;
        }

        [[nodiscard]] BigFloat operator-() const noexcept
        {
            BigFloat result   = *this;
            result.m_negative = !result.m_negative;
            return result;
        }

        [[nodiscard]] BigFloat operator+(const BigFloat& other) const
        {
            if (IsNaN() || other.IsNaN())
                return QuietNaN();
            if (IsInfinity() || other.IsInfinity())
            {
                if (IsInfinity() && other.IsInfinity() && m_negative != other.m_negative)
                    return QuietNaN();
                return IsInfinity() ? *this : other;
            }
            if (IsZero() && other.IsZero())
                return Zero(m_negative && other.m_negative);
            if (IsZero())
                return other;
            if (other.IsZero())
                return *this;

            const BigFloat* dominant = this;
            const BigFloat* smaller  = &other;
            if (m_exponent < other.m_exponent ||
                (m_exponent == other.m_exponent && m_significand.Compare(other.m_significand) < 0))
            {
                dominant = &other;
                smaller  = this;
            }
            const UInt64 exponentDifference = PositiveDifference(dominant->m_exponent, smaller->m_exponent);
            if (exponentDifference > PrecisionBits + GUARD_BITS)
                return RoundTinyAddition(*dominant, *smaller);

            detail::BinaryBigUInt dominantMagnitude = dominant->m_significand;
            dominantMagnitude.ShiftLeft(static_cast<UIntSize>(exponentDifference));
            detail::BinaryBigUInt resultMagnitude;
            bool                  resultNegative = false;
            if (dominant->m_negative == smaller->m_negative)
            {
                dominantMagnitude.Add(smaller->m_significand);
                resultMagnitude = std::move(dominantMagnitude);
                resultNegative  = dominant->m_negative;
            }
            else
            {
                const int comparison = dominantMagnitude.Compare(smaller->m_significand);
                if (comparison == 0)
                    return Zero(RoundingMode == BigFloatRoundingMode::TowardNegative);
                if (comparison > 0)
                {
                    dominantMagnitude.Subtract(smaller->m_significand);
                    resultMagnitude = std::move(dominantMagnitude);
                    resultNegative  = dominant->m_negative;
                }
                else
                {
                    resultMagnitude = smaller->m_significand;
                    resultMagnitude.Subtract(dominantMagnitude);
                    resultNegative = smaller->m_negative;
                }
            }
            const Int64 scale = CheckedSubtract(smaller->m_exponent, static_cast<Int64>(PrecisionBits - 1));
            return RoundFinite(std::move(resultMagnitude), scale, resultNegative, false);
        }

        [[nodiscard]] BigFloat operator-(const BigFloat& other) const { return *this + (-other); }

        [[nodiscard]] BigFloat operator*(const BigFloat& other) const
        {
            const bool negative = m_negative != other.m_negative;
            if (IsNaN() || other.IsNaN() ||
                ((IsInfinity() && other.IsZero()) || (IsZero() && other.IsInfinity())))
                return QuietNaN(negative);
            if (IsInfinity() || other.IsInfinity())
                return Infinity(negative);
            if (IsZero() || other.IsZero())
                return Zero(negative);

            detail::BinaryBigUInt product = detail::BinaryBigUInt::Multiply(m_significand, other.m_significand);
            const Int64           scale   = CheckedSubtract(
                    CheckedAdd(m_exponent, other.m_exponent),
                    static_cast<Int64>(2 * (PrecisionBits - 1)));
            return RoundFinite(std::move(product), scale, negative, false);
        }

        [[nodiscard]] BigFloat operator/(const BigFloat& other) const
        {
            const bool negative = m_negative != other.m_negative;
            if (IsNaN() || other.IsNaN() || (IsInfinity() && other.IsInfinity()) || (IsZero() && other.IsZero()))
                return QuietNaN(negative);
            if (IsInfinity() || other.IsZero())
                return Infinity(negative);
            if (IsZero() || other.IsInfinity())
                return Zero(negative);

            detail::BinaryBigUInt numerator = m_significand;
            numerator.ShiftLeft(PrecisionBits + GUARD_BITS);
            auto [quotient, remainder] = detail::BinaryBigUInt::DivRem(numerator, other.m_significand);
            const Int64 scale          = CheckedSubtract(
                    CheckedSubtract(m_exponent, other.m_exponent),
                    static_cast<Int64>(PrecisionBits + GUARD_BITS));
            return RoundFinite(std::move(quotient), scale, negative, !remainder.IsZero());
        }

        BigFloat& operator+=(const BigFloat& other) { return *this = *this + other; }
        BigFloat& operator-=(const BigFloat& other) { return *this = *this - other; }
        BigFloat& operator*=(const BigFloat& other) { return *this = *this * other; }
        BigFloat& operator/=(const BigFloat& other) { return *this = *this / other; }

        [[nodiscard]] bool operator==(const BigFloat& other) const noexcept
        {
            if (IsNaN() || other.IsNaN())
                return false;
            if (IsZero() && other.IsZero())
                return true;
            if (m_class != other.m_class || m_negative != other.m_negative)
                return false;
            return m_class != BigFloatClass::Finite ||
                   (m_exponent == other.m_exponent && m_significand.Compare(other.m_significand) == 0);
        }

        [[nodiscard]] std::partial_ordering operator<=>(const BigFloat& other) const noexcept
        {
            if (IsNaN() || other.IsNaN())
                return std::partial_ordering::unordered;
            if (*this == other)
                return std::partial_ordering::equivalent;
            if (m_negative != other.m_negative)
                return m_negative ? std::partial_ordering::less : std::partial_ordering::greater;
            if (IsInfinity())
                return m_negative ? std::partial_ordering::less : std::partial_ordering::greater;
            if (other.IsInfinity())
                return other.m_negative ? std::partial_ordering::greater : std::partial_ordering::less;
            if (IsZero())
                return other.m_negative ? std::partial_ordering::greater : std::partial_ordering::less;
            if (other.IsZero())
                return m_negative ? std::partial_ordering::less : std::partial_ordering::greater;

            int comparison = m_exponent == other.m_exponent
                                     ? m_significand.Compare(other.m_significand)
                                     : (m_exponent < other.m_exponent ? -1 : 1);
            if (m_negative)
                comparison = -comparison;
            return comparison < 0 ? std::partial_ordering::less : std::partial_ordering::greater;
        }

        /// @brief Returns the adjacent representable value in the positive direction.
        [[nodiscard]] BigFloat NextUp() const
        {
            if (IsNaN() || (IsInfinity() && !m_negative))
                return *this;
            if (IsInfinity())
                return MaximumFinite(true);
            if (IsZero())
                return MinimumPositive();
            if (m_negative)
                return -((-*this).NextDownPositive());
            return NextUpPositive();
        }

        /// @brief Returns the adjacent representable value in the negative direction.
        [[nodiscard]] BigFloat NextDown() const
        {
            if (IsNaN() || (IsInfinity() && m_negative))
                return *this;
            if (IsInfinity())
                return MaximumFinite(false);
            if (IsZero())
                return -MinimumPositive();
            if (m_negative)
                return -((-*this).NextUpPositive());
            return NextDownPositive();
        }

        /// @brief Calculates a square root, rounded once to this type's precision.
        [[nodiscard]] BigFloat Sqrt() const
        {
            if (IsNaN() || (m_negative && !IsZero()))
                return QuietNaN();
            if (IsInfinity() || IsZero())
                return *this;

            const Int64 rootExponent = FloorDivideByTwo(m_exponent);
            const Int64 targetShift  = CheckedSubtract(
                    static_cast<Int64>(PrecisionBits + GUARD_BITS - 1),
                    rootExponent);
            const Int64 radicandShift = CheckedAdd(
                    CheckedSubtract(m_exponent, static_cast<Int64>(PrecisionBits - 1)),
                    CheckedAdd(targetShift, targetShift));
            detail::BinaryBigUInt radicand = m_significand;
            if (radicandShift >= 0)
                radicand.ShiftLeft(static_cast<UIntSize>(radicandShift));
            else
                radicand.ShiftRight(static_cast<UIntSize>(-radicandShift));
            bool                  exact = false;
            detail::BinaryBigUInt root  = detail::BinaryBigUInt::SqrtFloor(radicand, &exact);
            return RoundFinite(std::move(root), -targetShift, false, !exact);
        }

        /// @brief Computes `left * right + addend` with a single final rounding.
        [[nodiscard]] static BigFloat Fma(const BigFloat& left, const BigFloat& right, const BigFloat& addend)
        {
            if (!left.IsFinite() || !right.IsFinite() || !addend.IsFinite())
                return left * right + addend;
            if (left.IsZero() || right.IsZero())
                return addend;

            detail::BinaryBigUInt product      = detail::BinaryBigUInt::Multiply(left.m_significand, right.m_significand);
            const Int64           productScale = CheckedSubtract(
                    CheckedAdd(left.m_exponent, right.m_exponent),
                    static_cast<Int64>(2 * (PrecisionBits - 1)));
            if (addend.IsZero())
                return RoundFinite(std::move(product), productScale, left.m_negative != right.m_negative, false);

            detail::BinaryBigUInt addendMagnitude = addend.m_significand;
            const Int64           addendScale     = CheckedSubtract(addend.m_exponent, static_cast<Int64>(PrecisionBits - 1));
            const Int64           commonScale     = std::min(productScale, addendScale);
            const UInt64          productShift    = PositiveDifference(productScale, commonScale);
            const UInt64          addendShift     = PositiveDifference(addendScale, commonScale);
            product.ShiftLeft(static_cast<UIntSize>(productShift));
            addendMagnitude.ShiftLeft(static_cast<UIntSize>(addendShift));
            const bool productNegative = left.m_negative != right.m_negative;
            if (productNegative == addend.m_negative)
            {
                product.Add(addendMagnitude);
                return RoundFinite(std::move(product), commonScale, productNegative, false);
            }
            const int comparison = product.Compare(addendMagnitude);
            if (comparison == 0)
                return Zero(RoundingMode == BigFloatRoundingMode::TowardNegative);
            if (comparison > 0)
            {
                product.Subtract(addendMagnitude);
                return RoundFinite(std::move(product), commonScale, productNegative, false);
            }
            addendMagnitude.Subtract(product);
            return RoundFinite(std::move(addendMagnitude), commonScale, addend.m_negative, false);
        }

        /// @brief Produces an exact normalized hexadecimal representation.
        [[nodiscard]] std::string ToHexString() const
        {
            if (IsNaN())
                return m_negative ? "-nan" : "nan";
            if (IsInfinity())
                return m_negative ? "-inf" : "inf";
            if (IsZero())
                return m_negative ? "-0x0p+0" : "0x0p+0";

            const UIntSize        fractionHexDigits = (PrecisionBits - 1 + 3) / 4;
            const UIntSize        alignedBits       = 1 + fractionHexDigits * 4;
            detail::BinaryBigUInt aligned           = m_significand;
            aligned.ShiftLeft(alignedBits - PrecisionBits);
            std::string hex = aligned.ToHex();
            if (hex.size() < fractionHexDigits + 1)
                hex.insert(0, fractionHexDigits + 1 - hex.size(), '0');
            std::string result = m_negative ? "-0x" : "0x";
            result.push_back(hex.front());
            if (fractionHexDigits != 0)
            {
                result.push_back('.');
                result.append(hex.begin() + 1, hex.end());
            }
            result.push_back('p');
            result.push_back(m_exponent >= 0 ? '+' : '-');
            result += UnsignedDecimal(m_exponent);
            return result;
        }

        /// @brief Produces a rounded decimal scientific representation.
        /// @param significantDigits Decimal significant digits; zero selects a round-trip-safe count.
        [[nodiscard]] std::string ToString(UIntSize significantDigits = 0) const
        {
            if (IsNaN())
                return m_negative ? "-nan" : "nan";
            if (IsInfinity())
                return m_negative ? "-inf" : "inf";
            if (IsZero())
                return m_negative ? "-0" : "0";
            if (significantDigits == 0)
                significantDigits = MAX_DECIMAL_DIGITS;
            if (significantDigits == 0)
                significantDigits = 1;

            const Int64 binaryScale = CheckedSubtract(m_exponent, static_cast<Int64>(PrecisionBits - 1));
            if (binaryScale > MAX_FORMAT_BINARY_SHIFT || binaryScale < -MAX_FORMAT_BINARY_SHIFT)
                return ToHexString();

            detail::BinaryBigUInt coefficient  = m_significand;
            Int64                 decimalPoint = 0;
            if (binaryScale >= 0)
            {
                coefficient.ShiftLeft(static_cast<UIntSize>(binaryScale));
                std::string decimal = coefficient.ToDecimal();
                decimalPoint        = static_cast<Int64>(decimal.size());
                return FormatScientific(std::move(decimal), decimalPoint, significantDigits, m_negative);
            }

            const UIntSize denominatorPower = static_cast<UIntSize>(-binaryScale);
            coefficient                     = detail::BinaryBigUInt::Multiply(
                    coefficient,
                    detail::BinaryBigUInt::PowerSmall(5, denominatorPower));
            std::string decimal = coefficient.ToDecimal();
            decimalPoint        = static_cast<Int64>(decimal.size()) - static_cast<Int64>(denominatorPower);
            return FormatScientific(std::move(decimal), decimalPoint, significantDigits, m_negative);
        }

        explicit operator F64() const
        {
            if (IsNaN())
                return std::numeric_limits<F64>::quiet_NaN();
            if (IsInfinity())
                return m_negative ? -std::numeric_limits<F64>::infinity() : std::numeric_limits<F64>::infinity();
            if (IsZero())
                return m_negative ? -0.0 : 0.0;

            detail::BinaryBigUInt magnitude = m_significand;
            Int64                 scale     = CheckedSubtract(m_exponent, static_cast<Int64>(PrecisionBits - 1));
            if (magnitude.BitLength() > 53)
            {
                const UIntSize discarded = magnitude.BitLength() - 53;
                const bool     round     = magnitude.TestBit(discarded - 1) &&
                                   (magnitude.AnyBitsBelow(discarded - 1) || magnitude.TestBit(discarded));
                magnitude.ShiftRight(discarded);
                if (round)
                    magnitude.AddSmall(1);
                scale = CheckedAdd(scale, static_cast<Int64>(discarded));
                if (magnitude.BitLength() > 53)
                {
                    magnitude.ShiftRight(1);
                    scale = CheckedAdd(scale, 1);
                }
            }
            F64 value = std::ldexp(static_cast<F64>(magnitude.LowUInt64()), ClampToInt(scale));
            return m_negative ? -value : value;
        }

        friend std::ostream& operator<<(std::ostream& stream, const BigFloat& value)
        {
            return stream << value.ToString();
        }

    private:
        static constexpr UIntSize GUARD_BITS                      = 4;
        static constexpr Int64    MAX_DECIMAL_CONVERSION_EXPONENT = 100000;
        static constexpr Int64    MAX_FORMAT_BINARY_SHIFT         = 100000;
        static constexpr Int64    MIN_BINARY_EXPONENT             = -1000000;
        static constexpr Int64    MAX_BINARY_EXPONENT             = 1000000;
        static constexpr UIntSize MAX_DECIMAL_DIGITS              = (PrecisionBits * 30103 + 99999) / 100000 + 2;

        static std::string_view CheckedStringView(const char* text)
        {
            if (text == nullptr)
                throw std::invalid_argument("BigFloat cannot parse a null string");
            return text;
        }

        static std::string Lowercase(std::string_view text)
        {
            std::string result(text);
            for (char& character: result)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return result;
        }

        [[nodiscard]] static BigFloat ParseHex(std::string_view text, bool negative)
        {
            detail::BinaryBigUInt digits;
            UIntSize              fractionalDigits = 0;
            bool                  sawDigit         = false;
            bool                  sawPoint         = false;
            UIntSize              position         = 2;
            for (; position < text.size(); ++position)
            {
                const unsigned char character = static_cast<unsigned char>(text[position]);
                UInt32              value     = 0;
                if (character >= '0' && character <= '9')
                    value = character - '0';
                else if (character >= 'a' && character <= 'f')
                    value = character - 'a' + 10;
                else if (character >= 'A' && character <= 'F')
                    value = character - 'A' + 10;
                else if (character == '.' && !sawPoint)
                {
                    sawPoint = true;
                    continue;
                }
                else
                {
                    break;
                }
                sawDigit = true;
                digits.MultiplySmall(16);
                digits.AddSmall(value);
                if (sawPoint)
                    ++fractionalDigits;
            }
            if (!sawDigit || position == text.size() || (text[position] != 'p' && text[position] != 'P'))
                throw std::invalid_argument("BigFloat has an invalid hexadecimal significand");
            ++position;

            bool exponentNegative = false;
            if (position < text.size() && (text[position] == '+' || text[position] == '-'))
            {
                exponentNegative = text[position] == '-';
                ++position;
            }
            if (position == text.size() || !std::isdigit(static_cast<unsigned char>(text[position])))
                throw std::invalid_argument("BigFloat has an invalid binary exponent");

            Int64           exponent           = 0;
            constexpr Int64 exponentParseLimit = MAX_BINARY_EXPONENT * 2;
            for (; position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])); ++position)
            {
                const Int32 digit = text[position] - '0';
                if (exponent > (exponentParseLimit - digit) / 10)
                    throw std::out_of_range("BigFloat binary exponent is too large");
                exponent = exponent * 10 + digit;
            }
            if (position != text.size())
                throw std::invalid_argument("BigFloat contains trailing characters");
            if (digits.IsZero())
                return Zero(negative);
            if (exponentNegative)
                exponent = -exponent;
            if (fractionalDigits > static_cast<UIntSize>(exponentParseLimit / 4))
                throw std::out_of_range("BigFloat hexadecimal significand is too long");
            const Int64 scale = CheckedSubtract(exponent, static_cast<Int64>(fractionalDigits * 4));
            return RoundFinite(std::move(digits), scale, negative, false);
        }

        template<std::integral T>
        void AssignIntegral(T value)
        {
            using Unsigned     = std::make_unsigned_t<T>;
            Unsigned magnitude = 0;
            if constexpr (std::signed_integral<T>)
            {
                m_negative = value < 0;
                magnitude  = m_negative ? Unsigned(0) - static_cast<Unsigned>(value) : static_cast<Unsigned>(value);
            }
            else
            {
                magnitude = value;
            }
            if (magnitude == 0)
                return;
            static_assert(sizeof(Unsigned) <= sizeof(UInt64), "BigFloat integral conversion supports up to 64 bits");
            *this = RoundFinite(detail::BinaryBigUInt(static_cast<UInt64>(magnitude)), 0, m_negative, false);
        }

        template<std::floating_point T>
        void AssignFloat(T value)
        {
            if (std::isnan(value))
            {
                *this = QuietNaN(std::signbit(value));
                return;
            }
            if (std::isinf(value))
            {
                *this = Infinity(std::signbit(value));
                return;
            }
            if (value == 0)
            {
                *this = Zero(std::signbit(value));
                return;
            }
            const bool    negative    = std::signbit(value);
            int           exponent    = 0;
            const T       fraction    = std::frexp(std::fabs(value), &exponent);
            constexpr int nativeBits  = std::numeric_limits<T>::digits;
            const UInt64  significand = static_cast<UInt64>(std::ldexp(fraction, nativeBits));
            *this                     = RoundFinite(
                    detail::BinaryBigUInt(significand),
                    static_cast<Int64>(exponent - nativeBits),
                    negative,
                    false);
        }

        [[nodiscard]] static BigFloat RoundRational(
                const detail::BinaryBigUInt& numerator,
                const detail::BinaryBigUInt& denominator,
                Int64                        binaryScale,
                bool                         negative)
        {
            Int64 ratioExponent = static_cast<Int64>(numerator.BitLength()) -
                                  static_cast<Int64>(denominator.BitLength());
            bool ratioBelowEstimate = false;
            if (ratioExponent >= 0)
            {
                detail::BinaryBigUInt shiftedDenominator = denominator;
                shiftedDenominator.ShiftLeft(static_cast<UIntSize>(ratioExponent));
                ratioBelowEstimate = numerator.Compare(shiftedDenominator) < 0;
            }
            else
            {
                detail::BinaryBigUInt shiftedNumerator = numerator;
                shiftedNumerator.ShiftLeft(static_cast<UIntSize>(-ratioExponent));
                ratioBelowEstimate = shiftedNumerator.Compare(denominator) < 0;
            }
            if (ratioBelowEstimate)
                --ratioExponent;

            const Int64           targetBit         = static_cast<Int64>(PrecisionBits + GUARD_BITS - 1);
            const Int64           shift             = targetBit - ratioExponent;
            detail::BinaryBigUInt scaledNumerator   = numerator;
            detail::BinaryBigUInt scaledDenominator = denominator;
            if (shift >= 0)
                scaledNumerator.ShiftLeft(static_cast<UIntSize>(shift));
            else
                scaledDenominator.ShiftLeft(static_cast<UIntSize>(-shift));
            auto [quotient, remainder] = detail::BinaryBigUInt::DivRem(scaledNumerator, scaledDenominator);
            return RoundFinite(
                    std::move(quotient),
                    CheckedSubtract(binaryScale, shift),
                    negative,
                    !remainder.IsZero());
        }

        [[nodiscard]] static BigFloat RoundFinite(
                detail::BinaryBigUInt magnitude,
                Int64                 scale,
                bool                  negative,
                bool                  externalSticky)
        {
            if (magnitude.IsZero())
                return Zero(negative);

            bool     inexact = externalSticky;
            UIntSize bits    = magnitude.BitLength();
            if (bits > PrecisionBits)
            {
                const UIntSize discarded        = bits - PrecisionBits;
                const bool     roundBit         = magnitude.TestBit(discarded - 1);
                const bool     sticky           = externalSticky || magnitude.AnyBitsBelow(discarded - 1);
                const bool     leastSignificant = magnitude.TestBit(discarded);
                const bool     discardedNonZero = roundBit || sticky;
                bool           increment        = false;
                inexact                         = inexact || discardedNonZero;
                if constexpr (RoundingMode == BigFloatRoundingMode::ToNearestEven)
                    increment = roundBit && (sticky || leastSignificant);
                else if constexpr (RoundingMode == BigFloatRoundingMode::TowardPositive)
                    increment = !negative && discardedNonZero;
                else if constexpr (RoundingMode == BigFloatRoundingMode::TowardNegative)
                    increment = negative && discardedNonZero;
                else if constexpr (RoundingMode == BigFloatRoundingMode::AwayFromZero)
                    increment = discardedNonZero;

                magnitude.ShiftRight(discarded);
                scale = CheckedAdd(scale, static_cast<Int64>(discarded));
                if (increment)
                    magnitude.AddSmall(1);
                if (magnitude.BitLength() > PrecisionBits)
                {
                    magnitude.ShiftRight(1);
                    scale = CheckedAdd(scale, 1);
                }
            }
            else if (bits < PrecisionBits)
            {
                const UIntSize shift = PrecisionBits - bits;
                magnitude.ShiftLeft(shift);
                scale = CheckedSubtract(scale, static_cast<Int64>(shift));
            }

            const Int64 exponent = CheckedAdd(scale, static_cast<Int64>(PrecisionBits - 1));
            if (exponent > MAX_BINARY_EXPONENT)
            {
                if constexpr (RoundingMode == BigFloatRoundingMode::TowardZero)
                    return MaximumFinite(negative);
                if constexpr (RoundingMode == BigFloatRoundingMode::TowardPositive)
                    return negative ? MaximumFinite(true) : Infinity();
                if constexpr (RoundingMode == BigFloatRoundingMode::TowardNegative)
                    return negative ? Infinity(true) : MaximumFinite(false);
                return Infinity(negative);
            }
            if (exponent < MIN_BINARY_EXPONENT)
            {
                if constexpr (RoundingMode == BigFloatRoundingMode::TowardPositive)
                    return negative ? Zero(true) : MinimumPositive();
                if constexpr (RoundingMode == BigFloatRoundingMode::TowardNegative)
                    return negative ? -MinimumPositive() : Zero();
                if constexpr (RoundingMode == BigFloatRoundingMode::AwayFromZero)
                    return negative ? -MinimumPositive() : MinimumPositive();
                if constexpr (RoundingMode == BigFloatRoundingMode::ToNearestEven)
                {
                    if (exponent == MIN_BINARY_EXPONENT - 1 && (!IsMinimumMagnitude(magnitude) || inexact))
                        return negative ? -MinimumPositive() : MinimumPositive();
                }
                return Zero(negative);
            }

            BigFloat result;
            result.m_class       = BigFloatClass::Finite;
            result.m_significand = std::move(magnitude);
            result.m_exponent    = exponent;
            result.m_negative    = negative;
            return result;
        }

        [[nodiscard]] static BigFloat RoundTinyAddition(const BigFloat& dominant, const BigFloat& smaller)
        {
            if constexpr (RoundingMode == BigFloatRoundingMode::TowardPositive)
                return smaller.m_negative ? dominant : dominant.NextUp();
            if constexpr (RoundingMode == BigFloatRoundingMode::TowardNegative)
                return smaller.m_negative ? dominant.NextDown() : dominant;
            if constexpr (RoundingMode == BigFloatRoundingMode::TowardZero)
            {
                if (dominant.m_negative != smaller.m_negative)
                    return dominant.m_negative ? dominant.NextUp() : dominant.NextDown();
            }
            if constexpr (RoundingMode == BigFloatRoundingMode::AwayFromZero)
            {
                if (dominant.m_negative == smaller.m_negative)
                    return dominant.m_negative ? dominant.NextDown() : dominant.NextUp();
            }
            return dominant;
        }

        [[nodiscard]] BigFloat NextUpPositive() const
        {
            BigFloat result = *this;
            result.m_significand.AddSmall(1);
            if (result.m_significand.BitLength() > PrecisionBits)
            {
                result.m_significand.ShiftRight(1);
                if (result.m_exponent == MAX_BINARY_EXPONENT)
                    return Infinity();
                ++result.m_exponent;
            }
            return result;
        }

        [[nodiscard]] BigFloat NextDownPositive() const
        {
            if (m_exponent == MIN_BINARY_EXPONENT && IsMinimumSignificand())
                return Zero();
            BigFloat result = *this;
            if (IsMinimumSignificand())
            {
                detail::BinaryBigUInt previous(1);
                previous.ShiftLeft(PrecisionBits);
                previous.Subtract(detail::BinaryBigUInt(1));
                result.m_significand = std::move(previous);
                --result.m_exponent;
            }
            else
            {
                result.m_significand.Subtract(detail::BinaryBigUInt(1));
            }
            return result;
        }

        [[nodiscard]] bool IsMinimumSignificand() const
        {
            return IsMinimumMagnitude(m_significand);
        }

        [[nodiscard]] static bool IsMinimumMagnitude(const detail::BinaryBigUInt& magnitude)
        {
            return magnitude.BitLength() == PrecisionBits && !magnitude.AnyBitsBelow(PrecisionBits - 1);
        }

        [[nodiscard]] static BigFloat MinimumPositive()
        {
            detail::BinaryBigUInt significand(1);
            significand.ShiftLeft(PrecisionBits - 1);
            BigFloat result;
            result.m_class       = BigFloatClass::Finite;
            result.m_significand = std::move(significand);
            result.m_exponent    = MIN_BINARY_EXPONENT;
            return result;
        }

        [[nodiscard]] static BigFloat MaximumFinite(bool negative)
        {
            detail::BinaryBigUInt significand(1);
            significand.ShiftLeft(PrecisionBits);
            significand.Subtract(detail::BinaryBigUInt(1));
            BigFloat result;
            result.m_class       = BigFloatClass::Finite;
            result.m_significand = std::move(significand);
            result.m_exponent    = MAX_BINARY_EXPONENT;
            result.m_negative    = negative;
            return result;
        }

        [[nodiscard]] static std::string FormatScientific(
                std::string decimal,
                Int64       decimalPoint,
                UIntSize    significantDigits,
                bool        negative)
        {
            Int64 exponent = decimalPoint - 1;
            if (decimal.size() > significantDigits)
            {
                const char roundDigit      = decimal[significantDigits];
                bool       trailingNonZero = false;
                for (UIntSize i = significantDigits + 1; i < decimal.size(); ++i)
                    trailingNonZero = trailingNonZero || decimal[i] != '0';
                const bool increment = roundDigit > '5' ||
                                       (roundDigit == '5' &&
                                        (trailingNonZero || ((decimal[significantDigits - 1] - '0') % 2 != 0)));
                decimal.resize(significantDigits);
                if (increment)
                {
                    UIntSize i = decimal.size();
                    while (i != 0 && decimal[i - 1] == '9')
                    {
                        decimal[i - 1] = '0';
                        --i;
                    }
                    if (i == 0)
                    {
                        decimal.assign(significantDigits, '0');
                        decimal[0] = '1';
                        ++exponent;
                    }
                    else
                    {
                        ++decimal[i - 1];
                    }
                }
            }
            else if (decimal.size() < significantDigits)
            {
                decimal.append(significantDigits - decimal.size(), '0');
            }

            std::string result;
            if (negative)
                result.push_back('-');
            result.push_back(decimal[0]);
            if (significantDigits > 1)
            {
                result.push_back('.');
                result.append(decimal.begin() + 1, decimal.end());
            }
            result.push_back('e');
            result.push_back(exponent >= 0 ? '+' : '-');
            result += UnsignedDecimal(exponent);
            return result;
        }

        [[nodiscard]] static std::string UnsignedDecimal(Int64 value)
        {
            const UInt64 magnitude = value < 0 ? static_cast<UInt64>(-(value + 1)) + 1 : static_cast<UInt64>(value);
            return std::to_string(magnitude);
        }

        [[nodiscard]] static Int64 CheckedAdd(Int64 left, Int64 right)
        {
            if ((right > 0 && left > (std::numeric_limits<Int64>::max)() - right) ||
                (right < 0 && left < (std::numeric_limits<Int64>::min)() - right))
                return right > 0 ? (std::numeric_limits<Int64>::max)() : (std::numeric_limits<Int64>::min)();
            return left + right;
        }

        [[nodiscard]] static Int64 CheckedSubtract(Int64 left, Int64 right)
        {
            if (right == (std::numeric_limits<Int64>::min)())
                return left >= 0 ? (std::numeric_limits<Int64>::max)() : left - right;
            return CheckedAdd(left, -right);
        }

        [[nodiscard]] static UInt64 PositiveDifference(Int64 greater, Int64 smaller) noexcept
        {
            const UInt64 unsignedGreater = static_cast<UInt64>(greater);
            const UInt64 unsignedSmaller = static_cast<UInt64>(smaller);
            return unsignedGreater - unsignedSmaller;
        }

        [[nodiscard]] static Int64 FloorDivideByTwo(Int64 value) noexcept
        {
            return value >= 0 || value % 2 == 0 ? value / 2 : value / 2 - 1;
        }

        [[nodiscard]] static int ClampToInt(Int64 value) noexcept
        {
            if (value > (std::numeric_limits<int>::max)())
                return (std::numeric_limits<int>::max)();
            if (value < (std::numeric_limits<int>::min)())
                return (std::numeric_limits<int>::min)();
            return static_cast<int>(value);
        }

        detail::BinaryBigUInt m_significand;
        Int64                 m_exponent = 0;
        BigFloatClass         m_class    = BigFloatClass::Zero;
        bool                  m_negative = false;
    };

    template<UIntSize PrecisionBits, BigFloatRoundingMode RoundingMode>
    [[nodiscard]] BigFloat<PrecisionBits, RoundingMode> Abs(const BigFloat<PrecisionBits, RoundingMode>& value)
    {
        return value.Abs();
    }

    template<UIntSize PrecisionBits, BigFloatRoundingMode RoundingMode>
    [[nodiscard]] BigFloat<PrecisionBits, RoundingMode> Sqrt(const BigFloat<PrecisionBits, RoundingMode>& value)
    {
        return value.Sqrt();
    }

    template<UIntSize PrecisionBits, BigFloatRoundingMode RoundingMode>
    [[nodiscard]] BigFloat<PrecisionBits, RoundingMode> Fma(
            const BigFloat<PrecisionBits, RoundingMode>& left,
            const BigFloat<PrecisionBits, RoundingMode>& right,
            const BigFloat<PrecisionBits, RoundingMode>& addend)
    {
        return BigFloat<PrecisionBits, RoundingMode>::Fma(left, right, addend);
    }
}// namespace NGIN::Math

namespace std
{
    /// @brief Standard numeric traits for NGIN's fixed-precision binary float.
    template<NGIN::UIntSize PrecisionBits, NGIN::Math::BigFloatRoundingMode RoundingMode>
    class numeric_limits<NGIN::Math::BigFloat<PrecisionBits, RoundingMode>>
    {
        using Value = NGIN::Math::BigFloat<PrecisionBits, RoundingMode>;

    public:
        static constexpr bool is_specialized = true;

        static Value min() noexcept { return Value::MinPositive(); }
        static Value lowest() noexcept { return Value::MaxFinite(true); }
        static Value max() noexcept { return Value::MaxFinite(); }
        static Value epsilon() { return Value(1).NextUp() - Value(1); }
        static Value round_error() { return Value("0.5"); }
        static Value infinity() noexcept { return Value::Infinity(); }
        static Value quiet_NaN() noexcept { return Value::QuietNaN(); }
        static Value signaling_NaN() noexcept { return Value::QuietNaN(); }
        static Value denorm_min() noexcept { return Value::MinPositive(); }

        static constexpr int                digits            = static_cast<int>(PrecisionBits);
        static constexpr int                digits10          = static_cast<int>((PrecisionBits - 1) * 30103 / 100000);
        static constexpr int                max_digits10      = static_cast<int>((PrecisionBits * 30103 + 99999) / 100000 + 1);
        static constexpr bool               is_signed         = true;
        static constexpr bool               is_integer        = false;
        static constexpr bool               is_exact          = false;
        static constexpr int                radix             = 2;
        static constexpr int                min_exponent      = -999999;
        static constexpr int                min_exponent10    = -301029;
        static constexpr int                max_exponent      = 1000001;
        static constexpr int                max_exponent10    = 301030;
        static constexpr bool               has_infinity      = true;
        static constexpr bool               has_quiet_NaN     = true;
        static constexpr bool               has_signaling_NaN = false;
        static constexpr float_denorm_style has_denorm        = denorm_absent;
        static constexpr bool               has_denorm_loss   = false;
        static constexpr bool               is_iec559         = false;
        static constexpr bool               is_bounded        = true;
        static constexpr bool               is_modulo         = false;
        static constexpr bool               traps             = false;
        static constexpr bool               tinyness_before   = false;
        static constexpr float_round_style  round_style       = [] {
            if constexpr (RoundingMode == NGIN::Math::BigFloatRoundingMode::ToNearestEven)
                return round_to_nearest;
            if constexpr (RoundingMode == NGIN::Math::BigFloatRoundingMode::TowardZero)
                return round_toward_zero;
            if constexpr (RoundingMode == NGIN::Math::BigFloatRoundingMode::TowardPositive)
                return round_toward_infinity;
            if constexpr (RoundingMode == NGIN::Math::BigFloatRoundingMode::TowardNegative)
                return round_toward_neg_infinity;
            return round_indeterminate;
        }();
    };
}// namespace std

/// @file LSBFlag.hpp
/// <summary>
/// Header-only utility to store a boolean flag in the least significant bit of an unsigned integral type.
/// </summary>
#pragma once

#include <type_traits>
#include <cstdint>
#include <limits>
#include <cassert>
#include <ostream>

namespace NGIN::Utilities
{

    /// <summary>
    /// Packs a boolean flag into the least significant bit of an unsigned integral value.
    /// </summary>
    /// <typeparam name="T">An unsigned integral type (e.g., uint8_t, uint32_t, uint64_t).</typeparam>
    template<typename T>
    class LSBFlag
    {
        static_assert(std::is_integral_v<T>, "LSBFlag can only be used with integral types.");
        static_assert(std::is_unsigned_v<T>, "LSBFlag requires an unsigned integral type.");
        static_assert(sizeof(T) > 0, "LSBFlag requires a non-zero-width type.");

    public:
        /// <summary>Number of bits in type T.</summary>
        static constexpr unsigned bitWidth = sizeof(T) * 8;
        /// <summary>Mask for the least significant bit (flag bit).</summary>
        static constexpr T flagMask = static_cast<T>(1);
        /// <summary>Mask for the value bits (all bits except LSB).</summary>
        static constexpr T valueMask = static_cast<T>(~flagMask);

        /// <summary>
        /// Default constructor: sets value = 0 and flag = false.
        /// </summary>
        constexpr LSBFlag() noexcept
            : data(T())
        {}

        /// <summary>
        /// Initializes with a value and a flag.
        /// </summary>
        /// <param name="value">The initial numeric value (must fit in bitWidth-1 bits).</param>
        /// <param name="flag">The initial flag state.</param>
        /// <remarks>
        /// Asserts in debug mode if value exceeds available bits.
        /// </remarks>
        constexpr LSBFlag(T value, bool flag) noexcept
            : data(static_cast<T>((value << 1) & valueMask) | static_cast<T>(flag ? flagMask : 0))
        {
            assert((value >> (bitWidth - 1)) == 0 && "LSBFlag: value exceeds available bits.");
        }

        /// <summary>
        /// Sets the numeric value, preserving the flag.
        /// </summary>
        /// <param name="value">New value (must fit in bitWidth-1 bits).</param>
        /// <remarks>Asserts in debug mode on overflow.</remarks>
        void SetValue(T value) noexcept
        {
            assert((value >> (bitWidth - 1)) == 0 && "LSBFlag: value exceeds available bits.");
            data = static_cast<T>((value << 1) & valueMask) | (data & flagMask);
        }

        /// <summary>
        /// Retrieves the stored numeric value (ignoring the flag bit).
        /// </summary>
        /// <returns>The stored value.</returns>
        [[nodiscard]] constexpr T GetValue() const noexcept
        {
            return static_cast<T>(data >> 1);
        }

        /// <summary>
        /// Sets the boolean flag, preserving the value bits.
        /// </summary>
        /// <param name="flag">New flag state.</param>
        void SetFlag(bool flag) noexcept
        {
            data = static_cast<T>((data & valueMask) | static_cast<T>(flag ? flagMask : 0));
        }

        /// <summary>
        /// Retrieves the boolean flag stored in the LSB.
        /// </summary>
        /// <returns>True if flag bit is set; otherwise, false.</returns>
        [[nodiscard]] constexpr bool GetFlag() const noexcept
        {
            return (data & flagMask) != 0;
        }

        /// <summary>
        /// Sets both the value and the flag at once.
        /// </summary>
        /// <param name="value">New value (must fit in bitWidth-1 bits).</param>
        /// <param name="flag">New flag state.</param>
        void Set(T value, bool flag) noexcept
        {
            assert((value >> (bitWidth - 1)) == 0 && "LSBFlag: value exceeds available bits.");
            data = static_cast<T>((value << 1) & valueMask) | static_cast<T>(flag ? flagMask : 0);
        }

        /// <summary>
        /// Retrieves the raw combined data (value and flag bit).
        /// </summary>
        /// <returns>The raw data.</returns>
        [[nodiscard]] constexpr T GetRaw() const noexcept
        {
            return data;
        }

        /// <summary>
        /// Overwrites the raw data directly (value and flag).
        /// </summary>
        /// <param name="rawData">New raw data.</param>
        void SetRaw(T rawData) noexcept
        {
            data = rawData;
        }

        /// <summary>
        /// Returns the maximum storable value (all bits shifted right by one, LSB reserved).
        /// </summary>
        /// <returns>The maximum value that can be stored.</returns>
        [[nodiscard]] static constexpr T MaxValue() noexcept
        {
            return static_cast<T>(std::numeric_limits<T>::max() >> 1);
        }

        /// <summary>
        /// Equality operator compares raw data.
        /// </summary>
        [[nodiscard]] constexpr bool operator==(const LSBFlag& other) const noexcept
        {
            return data == other.data;
        }

        /// <summary>
        /// Inequality operator compares raw data.
        /// </summary>
        [[nodiscard]] constexpr bool operator!=(const LSBFlag& other) const noexcept
        {
            return data != other.data;
        }

        /// <summary>
        /// Stream insertion: prints value and flag state.
        /// </summary>
        /// <param name="os">Output stream.</param>
        /// <param name="lf">The LSBFlag instance.</param>
        /// <returns>Reference to the output stream.</returns>
        friend std::ostream& operator<<(std::ostream& os, const LSBFlag& lf)
        {
            os << "Value=" << lf.GetValue();
            os << ", Flag=" << (lf.GetFlag() ? "true" : "false");
            return os;
        }

    private:
        T data;///< Underlying storage for value and flag.
    };

}// namespace NGIN::Utilities

/// @file MSBFlag.hpp
/// <summary>
/// Header-only utility to store a boolean flag in the most significant bit of an unsigned integral type.
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
    /// Packs a boolean flag into the most significant bit of an unsigned integral value.
    /// </summary>
    /// <typeparam name="T">An unsigned integral type (e.g., uint8_t, uint32_t, uint64_t).</typeparam>
    template<typename T>
    class MSBFlag
    {
        static_assert(std::is_integral_v<T>, "MSBFlag can only be used with integral types.");
        static_assert(std::is_unsigned_v<T>, "MSBFlag requires an unsigned integral type.");
        static_assert(sizeof(T) > 0, "MSBFlag requires a non-zero-width type.");

    public:
        /// <summary>Number of bits in type T.</summary>
        static constexpr unsigned bitWidth = sizeof(T) * 8;
        /// <summary>Mask for the MSB (flag bit).</summary>
        static constexpr T flagMask = static_cast<T>(T(1) << (bitWidth - 1));
        /// <summary>Mask for the value bits (all bits except MSB).</summary>
        static constexpr T valueMask = flagMask - 1;

        /// <summary>
        /// Default constructor: sets value = 0 and flag = false.
        /// </summary>
        constexpr MSBFlag() noexcept
            : data(T())
        {}

        /// <summary>
        /// Initializes with a value and a flag.
        /// </summary>
        /// <param name="value">The initial numeric value (must fit in bitWidth - 1 bits).</param>
        /// <param name="flag">The initial flag state.</param>
        /// <remarks>
        /// Asserts in debug mode if value exceeds available bits.
        /// </remarks>
        constexpr MSBFlag(T value, bool flag) noexcept
            : data((value & valueMask) | (flag ? flagMask : 0))
        {
            assert((value & flagMask) == 0 && "MSBFlag: value exceeds available bits.");
        }

        /// <summary>
        /// Sets the numeric value, preserving the flag.
        /// </summary>
        /// <param name="value">New value (must fit in bitWidth - 1 bits).</param>
        /// <remarks>Asserts in debug mode on overflow.</remarks>
        void SetValue(T value) noexcept
        {
            assert((value & flagMask) == 0 && "MSBFlag: value exceeds available bits.");
            data = (value & valueMask) | (data & flagMask);
        }

        /// <summary>
        /// Retrieves the stored numeric value (ignoring the flag bit).
        /// </summary>
        /// <returns>The lower bits storing the value.</returns>
        [[nodiscard]] constexpr T GetValue() const noexcept
        {
            return data & valueMask;
        }

        /// <summary>
        /// Sets the boolean flag, preserving the value bits.
        /// </summary>
        /// <param name="flag">New flag state.</param>
        void SetFlag(bool flag) noexcept
        {
            data = flag ? (data | flagMask) : (data & ~flagMask);
        }

        /// <summary>
        /// Retrieves the boolean flag stored in the MSB.
        /// </summary>
        /// <returns>True if flag bit is set; otherwise, false.</returns>
        [[nodiscard]] constexpr bool GetFlag() const noexcept
        {
            return (data & flagMask) != 0;
        }

        /// <summary>
        /// Sets both the value and the flag at once.
        /// </summary>
        /// <param name="value">New value (must fit in bitWidth - 1 bits).</param>
        /// <param name="flag">New flag state.</param>
        void Set(T value, bool flag) noexcept
        {
            assert((value & flagMask) == 0 && "MSBFlag: value exceeds available bits.");
            data = (value & valueMask) | (flag ? flagMask : 0);
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
        /// Returns the maximum storable value (all bits except MSB set).
        /// </summary>
        /// <returns>The maximum value that can be stored.</returns>
        [[nodiscard]] static constexpr T MaxValue() noexcept
        {
            return valueMask;
        }

        /// <summary>
        /// Equality operator compares raw data.
        /// </summary>
        [[nodiscard]] constexpr bool operator==(const MSBFlag& other) const noexcept
        {
            return data == other.data;
        }

        /// <summary>
        /// Inequality operator compares raw data.
        /// </summary>
        [[nodiscard]] constexpr bool operator!=(const MSBFlag& other) const noexcept
        {
            return data != other.data;
        }

        /// <summary>
        /// Stream insertion: prints value and flag state.
        /// </summary>
        /// <param name="os">Output stream.</param>
        /// <param name="mf">The MSBFlag instance.</param>
        /// <returns>Reference to the output stream.</returns>
        friend std::ostream& operator<<(std::ostream& os, const MSBFlag& mf)
        {
            os << "Value=" << mf.GetValue();
            os << ", Flag=" << (mf.GetFlag() ? "true" : "false");
            return os;
        }

    private:
        T data;///< Underlying storage for value and flag.
    };

}// namespace NGIN::Utilities

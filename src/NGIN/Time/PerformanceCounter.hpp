#pragma once

#include <NGIN/Primitives.hpp>

#include <cassert>
#include <limits>

namespace NGIN::Time::detail
{
    // QPC's counter can be large even at process startup. Scaling the whole
    // counter before dividing overflows after roughly 31 minutes at 10 MHz.
    constexpr UInt64 PerformanceCounterNanoseconds(UInt64 ticks, UInt64 frequency) noexcept
    {
        assert(frequency != 0);
        constexpr UInt64 scale   = 1'000'000'000;
        constexpr UInt64 maximum = std::numeric_limits<UInt64>::max();
        const UInt64     seconds = ticks / frequency;
        if (seconds > maximum / scale)
            return maximum;

        const UInt64 remainder = ticks % frequency;
        UInt64       fraction {};
        if (remainder <= maximum / scale)
            fraction = remainder * scale / frequency;
        else
        {
            // Unusually high frequencies can overflow even the fractional
            // product. Binary long multiplication keeps each residue < frequency
            // and computes the exact quotient without a compiler-specific integer.
            UInt64 residue {};
            for (UInt64 bit = UInt64 {1} << 29; bit != 0; bit >>= 1)
            {
                fraction *= 2;
                if (residue >= frequency - residue)
                {
                    residue -= frequency - residue;
                    ++fraction;
                }
                else
                    residue *= 2;
                if ((scale & bit) != 0)
                {
                    if (residue >= frequency - remainder)
                    {
                        residue -= frequency - remainder;
                        ++fraction;
                    }
                    else
                        residue += remainder;
                }
            }
        }
        const UInt64 whole = seconds * scale;
        return fraction > maximum - whole ? maximum : whole + fraction;
    }
}// namespace NGIN::Time::detail

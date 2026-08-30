#pragma once

/// @file Interpolation.hpp
/// @brief Scalar, vector, quaternion, and transform interpolation.

#include <NGIN/Math/Transform.hpp>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>

namespace NGIN::Math
{
    template<std::floating_point T>
    [[nodiscard]] constexpr T Lerp(T start, T end, T amount)
    {
        return start + (end - start) * amount;
    }

    template<std::floating_point T, std::size_t Dimension>
    [[nodiscard]] constexpr Vector<T, Dimension> Lerp(
            const Vector<T, Dimension>& start,
            const Vector<T, Dimension>& end,
            T                           amount)
    {
        return start + (end - start) * amount;
    }

    template<std::floating_point T, std::size_t Rows, std::size_t Columns>
    [[nodiscard]] constexpr Matrix<T, Rows, Columns> Lerp(
            const Matrix<T, Rows, Columns>& start,
            const Matrix<T, Rows, Columns>& end,
            T                               amount)
    {
        return start + (end - start) * amount;
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr T SmoothStep(T start, T end, T amount)
    {
        const T clamped = std::clamp(amount, T {0}, T {1});
        const T smooth  = clamped * clamped * (T {3} - T {2} * clamped);
        return Lerp(start, end, smooth);
    }

    /// @brief Normalized linear quaternion interpolation along the shortest arc.
    template<std::floating_point T>
    [[nodiscard]] Quaternion<T> NLerp(const Quaternion<T>& start, const Quaternion<T>& end, T amount)
    {
        const Quaternion<T> adjustedEnd = Dot(start, end) < T {0} ? -end : end;
        return Normalize(start + (adjustedEnd - start) * amount);
    }

    /// @brief Spherical quaternion interpolation along the shortest arc.
    template<std::floating_point T>
    [[nodiscard]] Quaternion<T> Slerp(const Quaternion<T>& start, const Quaternion<T>& end, T amount)
    {
        Quaternion<T> adjustedEnd = end;
        T             cosine      = Dot(start, end);
        if (cosine < T {0})
        {
            adjustedEnd = -end;
            cosine      = -cosine;
        }

        cosine = std::clamp(cosine, T {-1}, T {1});
        if (cosine > T {0.9995})
            return NLerp(start, adjustedEnd, amount);

        const T angle          = std::acos(cosine);
        const T reciprocalSine = T {1} / std::sin(angle);
        const T startWeight    = std::sin((T {1} - amount) * angle) * reciprocalSine;
        const T endWeight      = std::sin(amount * angle) * reciprocalSine;
        return Normalize(start * startWeight + adjustedEnd * endWeight);
    }

    template<std::floating_point T>
    [[nodiscard]] Transform2<T> Interpolate(const Transform2<T>& start, const Transform2<T>& end, T amount)
    {
        const T fullTurn   = T {2} * std::acos(T {-1});
        const T angleDelta = std::remainder(end.Rotation() - start.Rotation(), fullTurn);
        return {
                Lerp(start.Translation(), end.Translation(), amount),
                start.Rotation() + angleDelta * amount,
                Lerp(start.Scale(), end.Scale(), amount),
        };
    }

    template<std::floating_point T>
    [[nodiscard]] Transform3<T> Interpolate(const Transform3<T>& start, const Transform3<T>& end, T amount)
    {
        return {
                Lerp(start.Translation(), end.Translation(), amount),
                Slerp(start.Rotation(), end.Rotation(), amount),
                Lerp(start.Scale(), end.Scale(), amount),
        };
    }
}// namespace NGIN::Math

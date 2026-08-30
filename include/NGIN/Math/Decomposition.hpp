#pragma once

/// @file Decomposition.hpp
/// @brief Translation, rotation, and scale decomposition for affine matrices.

#include <NGIN/Math/Transform.hpp>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <optional>

namespace NGIN::Math
{
    /// @brief Decomposes a 2D affine matrix, rejecting perspective, shear, and degenerate scale.
    template<std::floating_point T>
    [[nodiscard]] std::optional<Transform2<T>> TryDecompose(const Matrix3<T>& matrix, T tolerance = T {1e-6})
    {
        const T absoluteTolerance = std::abs(tolerance);
        if (std::abs(matrix(2, 0)) > absoluteTolerance || std::abs(matrix(2, 1)) > absoluteTolerance ||
            std::abs(matrix(2, 2) - T {1}) > absoluteTolerance)
            return std::nullopt;

        Vector2<T> firstAxis {matrix(0, 0), matrix(1, 0)};
        Vector2<T> secondAxis {matrix(0, 1), matrix(1, 1)};
        T          firstScale  = Length(firstAxis);
        const T    secondScale = Length(secondAxis);
        if (firstScale <= absoluteTolerance || secondScale <= absoluteTolerance)
            return std::nullopt;

        firstAxis /= firstScale;
        secondAxis /= secondScale;
        if (std::abs(Dot(firstAxis, secondAxis)) > absoluteTolerance)
            return std::nullopt;

        const T determinant = firstAxis.X() * secondAxis.Y() - firstAxis.Y() * secondAxis.X();
        if (std::abs(std::abs(determinant) - T {1}) > absoluteTolerance * T {4})
            return std::nullopt;
        if (determinant < T {0})
        {
            firstScale = -firstScale;
            firstAxis  = -firstAxis;
        }

        return Transform2<T> {
                Vector2<T> {matrix(0, 2), matrix(1, 2)},
                std::atan2(firstAxis.Y(), firstAxis.X()),
                Vector2<T> {firstScale, secondScale},
        };
    }

    /// @brief Decomposes a 3D affine matrix, rejecting perspective, shear, and degenerate scale.
    template<std::floating_point T>
    [[nodiscard]] std::optional<Transform3<T>> TryDecompose(const Matrix4<T>& matrix, T tolerance = T {1e-6})
    {
        const T absoluteTolerance = std::abs(tolerance);
        if (std::abs(matrix(3, 0)) > absoluteTolerance || std::abs(matrix(3, 1)) > absoluteTolerance ||
            std::abs(matrix(3, 2)) > absoluteTolerance || std::abs(matrix(3, 3) - T {1}) > absoluteTolerance)
            return std::nullopt;

        Vector3<T> firstAxis {matrix(0, 0), matrix(1, 0), matrix(2, 0)};
        Vector3<T> secondAxis {matrix(0, 1), matrix(1, 1), matrix(2, 1)};
        Vector3<T> thirdAxis {matrix(0, 2), matrix(1, 2), matrix(2, 2)};
        T          firstScale  = Length(firstAxis);
        const T    secondScale = Length(secondAxis);
        const T    thirdScale  = Length(thirdAxis);
        if (firstScale <= absoluteTolerance || secondScale <= absoluteTolerance || thirdScale <= absoluteTolerance)
            return std::nullopt;

        firstAxis /= firstScale;
        secondAxis /= secondScale;
        thirdAxis /= thirdScale;
        const T maximumShear = std::max(
                {std::abs(Dot(firstAxis, secondAxis)),
                 std::abs(Dot(firstAxis, thirdAxis)),
                 std::abs(Dot(secondAxis, thirdAxis))});
        if (maximumShear > absoluteTolerance)
            return std::nullopt;

        const T determinant = Dot(Cross(firstAxis, secondAxis), thirdAxis);
        if (std::abs(std::abs(determinant) - T {1}) > absoluteTolerance * T {6})
            return std::nullopt;
        if (determinant < T {0})
        {
            firstScale = -firstScale;
            firstAxis  = -firstAxis;
        }

        const Matrix3<T> rotationMatrix {
                firstAxis.X(),
                secondAxis.X(),
                thirdAxis.X(),
                firstAxis.Y(),
                secondAxis.Y(),
                thirdAxis.Y(),
                firstAxis.Z(),
                secondAxis.Z(),
                thirdAxis.Z(),
        };
        return Transform3<T> {
                Vector3<T> {matrix(0, 3), matrix(1, 3), matrix(2, 3)},
                QuaternionFromRotationMatrix(rotationMatrix),
                Vector3<T> {firstScale, secondScale, thirdScale},
        };
    }

    template<std::floating_point T>
    [[nodiscard]] std::optional<Transform3<T>> TryDecompose(const AffineMatrix3<T>& matrix, T tolerance = T {1e-6})
    {
        return TryDecompose(ToMatrix4(matrix), tolerance);
    }
}// namespace NGIN::Math

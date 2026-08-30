#pragma once

/// @file Projection.hpp
/// @brief Explicit view and projection matrix constructors.

#include <NGIN/Math/Matrix.hpp>

#include <cassert>
#include <cmath>
#include <concepts>
#include <optional>

namespace NGIN::Math
{
    enum class Handedness
    {
        Right,
        Left,
    };

    enum class ClipDepthRange
    {
        NegativeOneToOne,
        ZeroToOne,
    };

    /// @brief Creates a perspective matrix for column-vector transforms.
    template<std::floating_point T>
    [[nodiscard]] Matrix4<T> Perspective(
            T              verticalFieldOfViewRadians,
            T              aspectRatio,
            T              nearPlane,
            T              farPlane,
            Handedness     handedness,
            ClipDepthRange depthRange)
    {
        assert(verticalFieldOfViewRadians > T {0});
        assert(verticalFieldOfViewRadians < std::acos(T {-1}));
        assert(aspectRatio > T {0});
        assert(nearPlane > T {0});
        assert(farPlane > nearPlane);

        const T    focalLength = T {1} / std::tan(verticalFieldOfViewRadians / T {2});
        Matrix4<T> result;
        result(0, 0) = focalLength / aspectRatio;
        result(1, 1) = focalLength;

        if (handedness == Handedness::Right)
        {
            result(3, 2) = T {-1};
            if (depthRange == ClipDepthRange::ZeroToOne)
            {
                result(2, 2) = farPlane / (nearPlane - farPlane);
                result(2, 3) = farPlane * nearPlane / (nearPlane - farPlane);
            }
            else
            {
                result(2, 2) = (farPlane + nearPlane) / (nearPlane - farPlane);
                result(2, 3) = T {2} * farPlane * nearPlane / (nearPlane - farPlane);
            }
        }
        else
        {
            result(3, 2) = T {1};
            if (depthRange == ClipDepthRange::ZeroToOne)
            {
                result(2, 2) = farPlane / (farPlane - nearPlane);
                result(2, 3) = -farPlane * nearPlane / (farPlane - nearPlane);
            }
            else
            {
                result(2, 2) = (farPlane + nearPlane) / (farPlane - nearPlane);
                result(2, 3) = -T {2} * farPlane * nearPlane / (farPlane - nearPlane);
            }
        }
        return result;
    }

    /// @brief Creates an orthographic matrix for column-vector transforms.
    template<std::floating_point T>
    [[nodiscard]] constexpr Matrix4<T> Orthographic(
            T              left,
            T              right,
            T              bottom,
            T              top,
            T              nearPlane,
            T              farPlane,
            Handedness     handedness,
            ClipDepthRange depthRange)
    {
        assert(right != left);
        assert(top != bottom);
        assert(farPlane != nearPlane);

        Matrix4<T> result = Matrix4<T>::Identity();
        result(0, 0)      = T {2} / (right - left);
        result(0, 3)      = -(right + left) / (right - left);
        result(1, 1)      = T {2} / (top - bottom);
        result(1, 3)      = -(top + bottom) / (top - bottom);

        const T direction = handedness == Handedness::Right ? T {-1} : T {1};
        if (depthRange == ClipDepthRange::ZeroToOne)
        {
            result(2, 2) = direction / (farPlane - nearPlane);
            result(2, 3) = -nearPlane / (farPlane - nearPlane);
        }
        else
        {
            result(2, 2) = direction * T {2} / (farPlane - nearPlane);
            result(2, 3) = -(farPlane + nearPlane) / (farPlane - nearPlane);
        }
        return result;
    }

    /// @brief Creates a view matrix from an eye, target, and approximate up direction.
    /// @pre Eye and target must differ and up must not be parallel to the viewing direction.
    template<std::floating_point T>
    [[nodiscard]] Matrix4<T> LookAt(
            const Vector3<T>& eye,
            const Vector3<T>& target,
            const Vector3<T>& up,
            Handedness        handedness)
    {
        const Vector3<T> forward = Normalize(target - eye);
        if (handedness == Handedness::Right)
        {
            const Vector3<T> side       = Normalize(Cross(forward, up));
            const Vector3<T> adjustedUp = Cross(side, forward);
            return Matrix4<T> {
                    side.X(),
                    side.Y(),
                    side.Z(),
                    -Dot(side, eye),
                    adjustedUp.X(),
                    adjustedUp.Y(),
                    adjustedUp.Z(),
                    -Dot(adjustedUp, eye),
                    -forward.X(),
                    -forward.Y(),
                    -forward.Z(),
                    Dot(forward, eye),
                    T {0},
                    T {0},
                    T {0},
                    T {1},
            };
        }

        const Vector3<T> side       = Normalize(Cross(up, forward));
        const Vector3<T> adjustedUp = Cross(forward, side);
        return Matrix4<T> {
                side.X(),
                side.Y(),
                side.Z(),
                -Dot(side, eye),
                adjustedUp.X(),
                adjustedUp.Y(),
                adjustedUp.Z(),
                -Dot(adjustedUp, eye),
                forward.X(),
                forward.Y(),
                forward.Z(),
                -Dot(forward, eye),
                T {0},
                T {0},
                T {0},
                T {1},
        };
    }

    /// @brief Creates a view matrix or returns no value for degenerate input.
    template<std::floating_point T>
    [[nodiscard]] std::optional<Matrix4<T>> TryLookAt(
            const Vector3<T>& eye,
            const Vector3<T>& target,
            const Vector3<T>& up,
            Handedness        handedness,
            T                 tolerance = T {0})
    {
        const std::optional<Vector3<T>> forward = TryNormalize(target - eye, tolerance);
        if (!forward)
            return std::nullopt;
        const Vector3<T> sideCandidate = handedness == Handedness::Right ? Cross(*forward, up) : Cross(up, *forward);
        if (!TryNormalize(sideCandidate, tolerance))
            return std::nullopt;
        return LookAt(eye, target, up, handedness);
    }
}// namespace NGIN::Math

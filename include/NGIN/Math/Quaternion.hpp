#pragma once

/// @file Quaternion.hpp
/// @brief Floating-point quaternions for three-dimensional rotations.

#include <NGIN/Math/Matrix.hpp>

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <optional>

namespace NGIN::Math
{
    /// @brief A Hamilton quaternion stored as x, y, z, w.
    /// @tparam T Floating-point component type.
    template<std::floating_point T>
    class Quaternion
    {
    public:
        using ValueType   = T;
        using StorageType = std::array<T, 4>;

        /// @brief Constructs the identity rotation.
        constexpr Quaternion() = default;

        /// @brief Constructs a quaternion in x, y, z, w order.
        constexpr Quaternion(T x, T y, T z, T w) : m_values {x, y, z, w} {}

        /// @brief Constructs a quaternion from contiguous x, y, z, w storage.
        constexpr explicit Quaternion(StorageType values) : m_values(values) {}

        /// @brief Returns the identity rotation.
        [[nodiscard]] static constexpr Quaternion Identity() noexcept { return {}; }

        [[nodiscard]] constexpr T&       X() noexcept { return m_values[0]; }
        [[nodiscard]] constexpr const T& X() const noexcept { return m_values[0]; }
        [[nodiscard]] constexpr T&       Y() noexcept { return m_values[1]; }
        [[nodiscard]] constexpr const T& Y() const noexcept { return m_values[1]; }
        [[nodiscard]] constexpr T&       Z() noexcept { return m_values[2]; }
        [[nodiscard]] constexpr const T& Z() const noexcept { return m_values[2]; }
        [[nodiscard]] constexpr T&       W() noexcept { return m_values[3]; }
        [[nodiscard]] constexpr const T& W() const noexcept { return m_values[3]; }

        [[nodiscard]] constexpr T*       Data() noexcept { return m_values.data(); }
        [[nodiscard]] constexpr const T* Data() const noexcept { return m_values.data(); }

        [[nodiscard]] constexpr bool operator==(const Quaternion&) const = default;

    private:
        StorageType m_values {T {0}, T {0}, T {0}, T {1}};
    };

    template<std::floating_point T>
    [[nodiscard]] constexpr Quaternion<T> operator+(const Quaternion<T>& left, const Quaternion<T>& right)
    {
        return {left.X() + right.X(), left.Y() + right.Y(), left.Z() + right.Z(), left.W() + right.W()};
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Quaternion<T> operator-(const Quaternion<T>& left, const Quaternion<T>& right)
    {
        return {left.X() - right.X(), left.Y() - right.Y(), left.Z() - right.Z(), left.W() - right.W()};
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Quaternion<T> operator-(const Quaternion<T>& value)
    {
        return {-value.X(), -value.Y(), -value.Z(), -value.W()};
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Quaternion<T> operator*(const Quaternion<T>& value, T scalar)
    {
        return {value.X() * scalar, value.Y() * scalar, value.Z() * scalar, value.W() * scalar};
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Quaternion<T> operator*(T scalar, const Quaternion<T>& value)
    {
        return value * scalar;
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Quaternion<T> operator/(const Quaternion<T>& value, T scalar)
    {
        return {value.X() / scalar, value.Y() / scalar, value.Z() / scalar, value.W() / scalar};
    }

    /// @brief Composes rotations using Hamilton multiplication.
    /// @details `parent * local` applies `local` first and then `parent` when rotating a column vector.
    template<std::floating_point T>
    [[nodiscard]] constexpr Quaternion<T> operator*(const Quaternion<T>& parent, const Quaternion<T>& local)
    {
        return Quaternion<T> {
                parent.W() * local.X() + parent.X() * local.W() + parent.Y() * local.Z() -
                        parent.Z() * local.Y(),
                parent.W() * local.Y() - parent.X() * local.Z() + parent.Y() * local.W() +
                        parent.Z() * local.X(),
                parent.W() * local.Z() + parent.X() * local.Y() - parent.Y() * local.X() +
                        parent.Z() * local.W(),
                parent.W() * local.W() - parent.X() * local.X() - parent.Y() * local.Y() -
                        parent.Z() * local.Z(),
        };
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr T Dot(const Quaternion<T>& left, const Quaternion<T>& right)
    {
        return left.X() * right.X() + left.Y() * right.Y() + left.Z() * right.Z() + left.W() * right.W();
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr T LengthSquared(const Quaternion<T>& value)
    {
        return Dot(value, value);
    }

    template<std::floating_point T>
    [[nodiscard]] T Length(const Quaternion<T>& value)
    {
        return std::sqrt(LengthSquared(value));
    }

    template<std::floating_point T>
    [[nodiscard]] Quaternion<T> Normalize(const Quaternion<T>& value)
    {
        return value / std::sqrt(LengthSquared(value));
    }

    template<std::floating_point T>
    [[nodiscard]] std::optional<Quaternion<T>> TryNormalize(const Quaternion<T>& value, T tolerance = T {0})
    {
        const T lengthSquared     = LengthSquared(value);
        const T absoluteTolerance = std::abs(tolerance);
        if (lengthSquared <= absoluteTolerance * absoluteTolerance)
            return std::nullopt;
        return value / std::sqrt(lengthSquared);
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Quaternion<T> Conjugate(const Quaternion<T>& value)
    {
        return {-value.X(), -value.Y(), -value.Z(), value.W()};
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Quaternion<T> Inverse(const Quaternion<T>& value)
    {
        return Conjugate(value) / LengthSquared(value);
    }

    template<std::floating_point T>
    [[nodiscard]] std::optional<Quaternion<T>> TryInverse(const Quaternion<T>& value, T tolerance = T {0})
    {
        const T lengthSquared     = LengthSquared(value);
        const T absoluteTolerance = std::abs(tolerance);
        if (lengthSquared <= absoluteTolerance * absoluteTolerance)
            return std::nullopt;
        return Conjugate(value) / lengthSquared;
    }

    /// @brief Creates a unit quaternion from an axis and an angle in radians.
    /// @pre `axis` must have non-zero length.
    template<std::floating_point T>
    [[nodiscard]] Quaternion<T> QuaternionFromAxisAngle(const Vector3<T>& axis, T radians)
    {
        const Vector3<T> normalizedAxis = Normalize(axis);
        const T          halfAngle      = radians / T {2};
        const T          sine           = std::sin(halfAngle);
        return {normalizedAxis.X() * sine, normalizedAxis.Y() * sine, normalizedAxis.Z() * sine, std::cos(halfAngle)};
    }

    /// @brief Creates a unit quaternion from an axis and angle, or no value for a degenerate axis.
    template<std::floating_point T>
    [[nodiscard]] std::optional<Quaternion<T>> TryQuaternionFromAxisAngle(
            const Vector3<T>& axis,
            T                 radians,
            T                 tolerance = T {0})
    {
        const std::optional<Vector3<T>> normalizedAxis = TryNormalize(axis, tolerance);
        if (!normalizedAxis)
            return std::nullopt;
        const T halfAngle = radians / T {2};
        const T sine      = std::sin(halfAngle);
        return Quaternion<T> {
                normalizedAxis->X() * sine,
                normalizedAxis->Y() * sine,
                normalizedAxis->Z() * sine,
                std::cos(halfAngle),
        };
    }

    /// @brief Converts a unit quaternion to a column-vector rotation matrix.
    template<std::floating_point T>
    [[nodiscard]] constexpr Matrix3<T> ToMatrix3(const Quaternion<T>& value)
    {
        const T xx = value.X() * value.X();
        const T yy = value.Y() * value.Y();
        const T zz = value.Z() * value.Z();
        const T xy = value.X() * value.Y();
        const T xz = value.X() * value.Z();
        const T yz = value.Y() * value.Z();
        const T wx = value.W() * value.X();
        const T wy = value.W() * value.Y();
        const T wz = value.W() * value.Z();

        return Matrix3<T> {
                T {1} - T {2} * (yy + zz),
                T {2} * (xy - wz),
                T {2} * (xz + wy),
                T {2} * (xy + wz),
                T {1} - T {2} * (xx + zz),
                T {2} * (yz - wx),
                T {2} * (xz - wy),
                T {2} * (yz + wx),
                T {1} - T {2} * (xx + yy),
        };
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Matrix4<T> ToMatrix4(const Quaternion<T>& value)
    {
        const Matrix3<T> rotation = ToMatrix3(value);
        return Matrix4<T> {
                rotation(0, 0),
                rotation(0, 1),
                rotation(0, 2),
                T {0},
                rotation(1, 0),
                rotation(1, 1),
                rotation(1, 2),
                T {0},
                rotation(2, 0),
                rotation(2, 1),
                rotation(2, 2),
                T {0},
                T {0},
                T {0},
                T {0},
                T {1},
        };
    }

    /// @brief Creates a quaternion from an orthonormal column-vector rotation matrix.
    template<std::floating_point T>
    [[nodiscard]] Quaternion<T> QuaternionFromRotationMatrix(const Matrix3<T>& matrix)
    {
        Quaternion<T> result;
        const T       trace = matrix(0, 0) + matrix(1, 1) + matrix(2, 2);
        if (trace > T {0})
        {
            const T scale = T {2} * std::sqrt(trace + T {1});
            result.W()    = scale / T {4};
            result.X()    = (matrix(2, 1) - matrix(1, 2)) / scale;
            result.Y()    = (matrix(0, 2) - matrix(2, 0)) / scale;
            result.Z()    = (matrix(1, 0) - matrix(0, 1)) / scale;
        }
        else if (matrix(0, 0) > matrix(1, 1) && matrix(0, 0) > matrix(2, 2))
        {
            const T scale = T {2} * std::sqrt(T {1} + matrix(0, 0) - matrix(1, 1) - matrix(2, 2));
            result.W()    = (matrix(2, 1) - matrix(1, 2)) / scale;
            result.X()    = scale / T {4};
            result.Y()    = (matrix(0, 1) + matrix(1, 0)) / scale;
            result.Z()    = (matrix(0, 2) + matrix(2, 0)) / scale;
        }
        else if (matrix(1, 1) > matrix(2, 2))
        {
            const T scale = T {2} * std::sqrt(T {1} + matrix(1, 1) - matrix(0, 0) - matrix(2, 2));
            result.W()    = (matrix(0, 2) - matrix(2, 0)) / scale;
            result.X()    = (matrix(0, 1) + matrix(1, 0)) / scale;
            result.Y()    = scale / T {4};
            result.Z()    = (matrix(1, 2) + matrix(2, 1)) / scale;
        }
        else
        {
            const T scale = T {2} * std::sqrt(T {1} + matrix(2, 2) - matrix(0, 0) - matrix(1, 1));
            result.W()    = (matrix(1, 0) - matrix(0, 1)) / scale;
            result.X()    = (matrix(0, 2) + matrix(2, 0)) / scale;
            result.Y()    = (matrix(1, 2) + matrix(2, 1)) / scale;
            result.Z()    = scale / T {4};
        }
        return Normalize(result);
    }

    /// @brief Rotates a vector by a unit quaternion.
    template<std::floating_point T>
    [[nodiscard]] constexpr Vector3<T> Rotate(const Quaternion<T>& rotation, const Vector3<T>& vector)
    {
        const Vector3<T> imaginary {rotation.X(), rotation.Y(), rotation.Z()};
        const Vector3<T> twiceCross = T {2} * Cross(imaginary, vector);
        return vector + rotation.W() * twiceCross + Cross(imaginary, twiceCross);
    }

    using QuaternionF = Quaternion<F32>;
    using QuaternionD = Quaternion<F64>;
}// namespace NGIN::Math

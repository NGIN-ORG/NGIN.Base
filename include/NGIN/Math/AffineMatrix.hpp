#pragma once

/// @file AffineMatrix.hpp
/// @brief Compact three-dimensional affine transformation matrices.

#include <NGIN/Math/Matrix.hpp>

#include <cmath>
#include <concepts>
#include <cstddef>
#include <optional>
#include <utility>

namespace NGIN::Math
{
    /// @brief A row-major 3x4 affine matrix acting on column vectors.
    /// @details The implicit fourth row is `[0, 0, 0, 1]`.
    template<std::floating_point T>
    class AffineMatrix3
    {
    public:
        using ValueType   = T;
        using StorageType = typename Matrix<T, 3, 4>::StorageType;

        /// @brief Constructs the identity affine transform.
        constexpr AffineMatrix3() : m_matrix {
                                            T {1}, T {0}, T {0}, T {0},
                                            T {0}, T {1}, T {0}, T {0},
                                            T {0}, T {0}, T {1}, T {0}}
        {
        }

        /// @brief Constructs an affine matrix from twelve row-major elements.
        template<class... Values>
            requires(sizeof...(Values) == 12 && (std::constructible_from<T, Values &&> && ...))
        constexpr explicit AffineMatrix3(Values&&... values)
            : m_matrix(std::forward<Values>(values)...)
        {
        }

        /// @brief Constructs an affine matrix from its linear part and translation.
        constexpr AffineMatrix3(const Matrix3<T>& linear, const Vector3<T>& translation)
            : m_matrix {
                      linear(0, 0), linear(0, 1), linear(0, 2), translation.X(),
                      linear(1, 0), linear(1, 1), linear(1, 2), translation.Y(),
                      linear(2, 0), linear(2, 1), linear(2, 2), translation.Z()}
        {
        }

        /// @brief Constructs an affine matrix from raw row-major storage.
        constexpr explicit AffineMatrix3(StorageType values) : m_matrix(std::move(values)) {}

        [[nodiscard]] static constexpr AffineMatrix3 Identity() noexcept { return {}; }

        [[nodiscard]] constexpr T& operator()(std::size_t row, std::size_t column) noexcept
        {
            return m_matrix(row, column);
        }

        [[nodiscard]] constexpr const T& operator()(std::size_t row, std::size_t column) const noexcept
        {
            return m_matrix(row, column);
        }

        [[nodiscard]] constexpr T*       Data() noexcept { return m_matrix.Data(); }
        [[nodiscard]] constexpr const T* Data() const noexcept { return m_matrix.Data(); }

        [[nodiscard]] constexpr Matrix3<T> Linear() const
        {
            return Matrix3<T> {
                    m_matrix(0, 0),
                    m_matrix(0, 1),
                    m_matrix(0, 2),
                    m_matrix(1, 0),
                    m_matrix(1, 1),
                    m_matrix(1, 2),
                    m_matrix(2, 0),
                    m_matrix(2, 1),
                    m_matrix(2, 2),
            };
        }

        [[nodiscard]] constexpr Vector3<T> Translation() const
        {
            return Vector3<T> {m_matrix(0, 3), m_matrix(1, 3), m_matrix(2, 3)};
        }

        constexpr void SetLinear(const Matrix3<T>& linear)
        {
            for (std::size_t row = 0; row < 3; ++row)
                for (std::size_t column = 0; column < 3; ++column)
                    m_matrix(row, column) = linear(row, column);
        }

        constexpr void SetTranslation(const Vector3<T>& translation)
        {
            m_matrix(0, 3) = translation.X();
            m_matrix(1, 3) = translation.Y();
            m_matrix(2, 3) = translation.Z();
        }

        [[nodiscard]] constexpr const Matrix<T, 3, 4>& MatrixValue() const noexcept { return m_matrix; }

        [[nodiscard]] constexpr bool operator==(const AffineMatrix3&) const = default;

    private:
        Matrix<T, 3, 4> m_matrix;
    };

    /// @brief Transforms a point, including translation.
    template<std::floating_point T>
    [[nodiscard]] constexpr Vector3<T> TransformPoint(const AffineMatrix3<T>& matrix, const Vector3<T>& point)
    {
        return Vector3<T> {
                matrix(0, 0) * point.X() + matrix(0, 1) * point.Y() + matrix(0, 2) * point.Z() + matrix(0, 3),
                matrix(1, 0) * point.X() + matrix(1, 1) * point.Y() + matrix(1, 2) * point.Z() + matrix(1, 3),
                matrix(2, 0) * point.X() + matrix(2, 1) * point.Y() + matrix(2, 2) * point.Z() + matrix(2, 3),
        };
    }

    /// @brief Transforms a direction without translation.
    template<std::floating_point T>
    [[nodiscard]] constexpr Vector3<T> TransformDirection(
            const AffineMatrix3<T>& matrix,
            const Vector3<T>&       direction)
    {
        return matrix.Linear() * direction;
    }

    /// @brief Composes affine transforms for column-vector use.
    template<std::floating_point T>
    [[nodiscard]] constexpr AffineMatrix3<T> operator*(const AffineMatrix3<T>& parent, const AffineMatrix3<T>& local)
    {
        const Matrix3<T> linear      = parent.Linear() * local.Linear();
        const Vector3<T> translation = parent.Linear() * local.Translation() + parent.Translation();
        return {linear, translation};
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Matrix4<T> ToMatrix4(const AffineMatrix3<T>& value)
    {
        return Matrix4<T> {
                value(0, 0),
                value(0, 1),
                value(0, 2),
                value(0, 3),
                value(1, 0),
                value(1, 1),
                value(1, 2),
                value(1, 3),
                value(2, 0),
                value(2, 1),
                value(2, 2),
                value(2, 3),
                T {0},
                T {0},
                T {0},
                T {1},
        };
    }

    /// @brief Converts a 4x4 affine matrix to compact storage.
    /// @pre The last row must be `[0, 0, 0, 1]`.
    template<std::floating_point T>
    [[nodiscard]] constexpr AffineMatrix3<T> ToAffineMatrix3(const Matrix4<T>& value)
    {
        return AffineMatrix3<T> {
                value(0, 0),
                value(0, 1),
                value(0, 2),
                value(0, 3),
                value(1, 0),
                value(1, 1),
                value(1, 2),
                value(1, 3),
                value(2, 0),
                value(2, 1),
                value(2, 2),
                value(2, 3),
        };
    }

    template<std::floating_point T>
    [[nodiscard]] std::optional<AffineMatrix3<T>> TryToAffineMatrix3(const Matrix4<T>& value, T tolerance = T {0})
    {
        const T absoluteTolerance = std::abs(tolerance);
        if (std::abs(value(3, 0)) > absoluteTolerance || std::abs(value(3, 1)) > absoluteTolerance ||
            std::abs(value(3, 2)) > absoluteTolerance || std::abs(value(3, 3) - T {1}) > absoluteTolerance)
            return std::nullopt;
        return ToAffineMatrix3(value);
    }

    template<std::floating_point T>
    [[nodiscard]] AffineMatrix3<T> Inverse(const AffineMatrix3<T>& value)
    {
        const Matrix3<T> inverseLinear = Inverse(value.Linear());
        return {inverseLinear, -(inverseLinear * value.Translation())};
    }

    template<std::floating_point T>
    [[nodiscard]] std::optional<AffineMatrix3<T>> TryInverse(const AffineMatrix3<T>& value, T tolerance = T {0})
    {
        const std::optional<Matrix3<T>> inverseLinear = TryInverse(value.Linear(), tolerance);
        if (!inverseLinear)
            return std::nullopt;
        return AffineMatrix3<T> {*inverseLinear, -(*inverseLinear * value.Translation())};
    }

    using AffineMatrix3F = AffineMatrix3<F32>;
    using AffineMatrix3D = AffineMatrix3<F64>;
}// namespace NGIN::Math

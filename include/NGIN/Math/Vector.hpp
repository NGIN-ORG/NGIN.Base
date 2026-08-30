#pragma once

/// @file Vector.hpp
/// @brief Fixed-size mathematical vectors and their fundamental operations.

#include <NGIN/Primitives.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <optional>
#include <utility>

namespace NGIN::Math
{
    /// @brief Numeric types that support the field-like operations used by linear algebra values.
    /// @details The concept checks syntax only. Algorithms involving division require the type's
    /// division to have the mathematical semantics expected by the algorithm.
    template<class T>
    concept LinearAlgebraScalarConcept = std::regular<T> && requires(T value, const T other) {
        T {0};
        T {1};
        { other + other } -> std::convertible_to<T>;
        { other - other } -> std::convertible_to<T>;
        { other * other } -> std::convertible_to<T>;
        { other / other } -> std::convertible_to<T>;
        { -other } -> std::convertible_to<T>;
        { value += other } -> std::same_as<T&>;
        { value -= other } -> std::same_as<T&>;
        { value *= other } -> std::same_as<T&>;
        { value /= other } -> std::same_as<T&>;
    };

    /// @brief A contiguous, fixed-size mathematical vector.
    /// @tparam T Component type.
    /// @tparam Dimension Number of vector components; must be greater than zero.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    class Vector
    {
        static_assert(Dimension > 0, "A mathematical vector must have at least one component");

    public:
        /// @brief Component type.
        using ValueType = T;

        /// @brief Contiguous component storage.
        using StorageType = std::array<T, Dimension>;

        /// @brief Number of components.
        static constexpr std::size_t DIMENSION = Dimension;

        /// @brief Constructs the zero vector.
        constexpr Vector() = default;

        /// @brief Constructs a vector from exactly one value per component.
        template<class... Values>
            requires(sizeof...(Values) == Dimension && (std::constructible_from<T, Values &&> && ...))
        constexpr explicit Vector(Values&&... values)
            : m_values {static_cast<T>(std::forward<Values>(values))...}
        {
        }

        /// @brief Constructs a vector from contiguous array storage.
        constexpr explicit Vector(StorageType values) : m_values(std::move(values)) {}

        /// @brief Explicitly converts every component of another vector.
        template<LinearAlgebraScalarConcept U>
            requires std::constructible_from<T, const U&>
        constexpr explicit Vector(const Vector<U, Dimension>& other)
        {
            for (std::size_t index = 0; index < Dimension; ++index)
                m_values[index] = static_cast<T>(other[index]);
        }

        /// @brief Creates a vector with every component set to the same value.
        [[nodiscard]] static constexpr Vector Filled(const T& value)
        {
            Vector result;
            result.m_values.fill(value);
            return result;
        }

        /// @brief Returns mutable contiguous component storage.
        [[nodiscard]] constexpr T* Data() noexcept { return m_values.data(); }

        /// @brief Returns immutable contiguous component storage.
        [[nodiscard]] constexpr const T* Data() const noexcept { return m_values.data(); }

        /// @brief Returns the fixed number of components.
        [[nodiscard]] static constexpr std::size_t Size() noexcept { return Dimension; }

        /// @brief Returns a mutable component by zero-based index.
        [[nodiscard]] constexpr T& operator[](std::size_t index) noexcept
        {
            assert(index < Dimension);
            return m_values[index];
        }

        /// @brief Returns an immutable component by zero-based index.
        [[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept
        {
            assert(index < Dimension);
            return m_values[index];
        }

        /// @brief Returns the first component.
        [[nodiscard]] constexpr T& X() noexcept { return m_values[0]; }

        /// @brief Returns the first component.
        [[nodiscard]] constexpr const T& X() const noexcept { return m_values[0]; }

        /// @brief Returns the second component.
        [[nodiscard]] constexpr T& Y() noexcept
            requires(Dimension >= 2)
        {
            return m_values[1];
        }

        /// @brief Returns the second component.
        [[nodiscard]] constexpr const T& Y() const noexcept
            requires(Dimension >= 2)
        {
            return m_values[1];
        }

        /// @brief Returns the third component.
        [[nodiscard]] constexpr T& Z() noexcept
            requires(Dimension >= 3)
        {
            return m_values[2];
        }

        /// @brief Returns the third component.
        [[nodiscard]] constexpr const T& Z() const noexcept
            requires(Dimension >= 3)
        {
            return m_values[2];
        }

        /// @brief Returns the fourth component.
        [[nodiscard]] constexpr T& W() noexcept
            requires(Dimension >= 4)
        {
            return m_values[3];
        }

        /// @brief Returns the fourth component.
        [[nodiscard]] constexpr const T& W() const noexcept
            requires(Dimension >= 4)
        {
            return m_values[3];
        }

        /// @brief Returns an iterator to the first component.
        [[nodiscard]] constexpr auto begin() noexcept { return m_values.begin(); }

        /// @brief Returns an immutable iterator to the first component.
        [[nodiscard]] constexpr auto begin() const noexcept { return m_values.begin(); }

        /// @brief Returns an iterator past the last component.
        [[nodiscard]] constexpr auto end() noexcept { return m_values.end(); }

        /// @brief Returns an immutable iterator past the last component.
        [[nodiscard]] constexpr auto end() const noexcept { return m_values.end(); }

        /// @brief Adds another vector component-wise.
        constexpr Vector& operator+=(const Vector& other)
        {
            for (std::size_t index = 0; index < Dimension; ++index)
                m_values[index] += other.m_values[index];
            return *this;
        }

        /// @brief Subtracts another vector component-wise.
        constexpr Vector& operator-=(const Vector& other)
        {
            for (std::size_t index = 0; index < Dimension; ++index)
                m_values[index] -= other.m_values[index];
            return *this;
        }

        /// @brief Multiplies every component by a scalar.
        constexpr Vector& operator*=(const T& scalar)
        {
            for (T& component: m_values)
                component *= scalar;
            return *this;
        }

        /// @brief Divides every component by a scalar.
        constexpr Vector& operator/=(const T& scalar)
        {
            for (T& component: m_values)
                component /= scalar;
            return *this;
        }

        /// @brief Compares vectors component-wise for exact equality.
        [[nodiscard]] constexpr bool operator==(const Vector&) const = default;

    private:
        StorageType m_values {};
    };

    /// @brief Adds two vectors component-wise.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    [[nodiscard]] constexpr Vector<T, Dimension> operator+(Vector<T, Dimension> left, const Vector<T, Dimension>& right)
    {
        left += right;
        return left;
    }

    /// @brief Subtracts two vectors component-wise.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    [[nodiscard]] constexpr Vector<T, Dimension> operator-(Vector<T, Dimension> left, const Vector<T, Dimension>& right)
    {
        left -= right;
        return left;
    }

    /// @brief Negates every component.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    [[nodiscard]] constexpr Vector<T, Dimension> operator-(const Vector<T, Dimension>& value)
    {
        Vector<T, Dimension> result;
        for (std::size_t index = 0; index < Dimension; ++index)
            result[index] = -value[index];
        return result;
    }

    /// @brief Multiplies every vector component by a scalar.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    [[nodiscard]] constexpr Vector<T, Dimension> operator*(Vector<T, Dimension> value, const T& scalar)
    {
        value *= scalar;
        return value;
    }

    /// @brief Multiplies every vector component by a scalar.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    [[nodiscard]] constexpr Vector<T, Dimension> operator*(const T& scalar, Vector<T, Dimension> value)
    {
        value *= scalar;
        return value;
    }

    /// @brief Divides every vector component by a scalar.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    [[nodiscard]] constexpr Vector<T, Dimension> operator/(Vector<T, Dimension> value, const T& scalar)
    {
        value /= scalar;
        return value;
    }

    /// @brief Computes the component-wise product of two vectors.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    [[nodiscard]] constexpr Vector<T, Dimension> HadamardProduct(
            const Vector<T, Dimension>& left,
            const Vector<T, Dimension>& right)
    {
        Vector<T, Dimension> result;
        for (std::size_t index = 0; index < Dimension; ++index)
            result[index] = left[index] * right[index];
        return result;
    }

    /// @brief Computes the dot product of two vectors.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    [[nodiscard]] constexpr T Dot(const Vector<T, Dimension>& left, const Vector<T, Dimension>& right)
    {
        T result {0};
        for (std::size_t index = 0; index < Dimension; ++index)
            result += left[index] * right[index];
        return result;
    }

    /// @brief Computes the squared Euclidean length without taking a square root.
    template<LinearAlgebraScalarConcept T, std::size_t Dimension>
    [[nodiscard]] constexpr T LengthSquared(const Vector<T, Dimension>& value)
    {
        return Dot(value, value);
    }

    /// @brief Computes the Euclidean length of a floating-point vector.
    template<std::floating_point T, std::size_t Dimension>
    [[nodiscard]] T Length(const Vector<T, Dimension>& value)
    {
        return std::sqrt(LengthSquared(value));
    }

    /// @brief Computes the Euclidean distance between floating-point vectors.
    template<std::floating_point T, std::size_t Dimension>
    [[nodiscard]] T Distance(const Vector<T, Dimension>& left, const Vector<T, Dimension>& right)
    {
        return Length(right - left);
    }

    /// @brief Returns a normalized vector.
    /// @pre The vector must have non-zero length.
    /// @details Use TryNormalize() when degenerate inputs are possible.
    template<std::floating_point T, std::size_t Dimension>
    [[nodiscard]] Vector<T, Dimension> Normalize(const Vector<T, Dimension>& value)
    {
        const T reciprocalMagnitude = T {1} / std::sqrt(LengthSquared(value));
        return value * reciprocalMagnitude;
    }

    /// @brief Returns a normalized vector, or no value when its length is within the supplied tolerance of zero.
    template<std::floating_point T, std::size_t Dimension>
    [[nodiscard]] std::optional<Vector<T, Dimension>> TryNormalize(
            const Vector<T, Dimension>& value,
            T                           tolerance = T {0})
    {
        const T magnitudeSquared  = LengthSquared(value);
        const T absoluteTolerance = std::abs(tolerance);
        if (magnitudeSquared <= absoluteTolerance * absoluteTolerance)
            return std::nullopt;
        const T reciprocalMagnitude = T {1} / std::sqrt(magnitudeSquared);
        return value * reciprocalMagnitude;
    }

    /// @brief Computes the three-dimensional cross product.
    template<LinearAlgebraScalarConcept T>
    [[nodiscard]] constexpr Vector<T, 3> Cross(const Vector<T, 3>& left, const Vector<T, 3>& right)
    {
        return Vector<T, 3> {
                left.Y() * right.Z() - left.Z() * right.Y(),
                left.Z() * right.X() - left.X() * right.Z(),
                left.X() * right.Y() - left.Y() * right.X(),
        };
    }

    /// @brief Two-component vector alias.
    template<LinearAlgebraScalarConcept T>
    using Vector2 = Vector<T, 2>;

    /// @brief Three-component vector alias.
    template<LinearAlgebraScalarConcept T>
    using Vector3 = Vector<T, 3>;

    /// @brief Four-component vector alias.
    template<LinearAlgebraScalarConcept T>
    using Vector4 = Vector<T, 4>;

    /// @brief Common single-precision vector aliases.
    using Vector2F = Vector2<F32>;
    using Vector3F = Vector3<F32>;
    using Vector4F = Vector4<F32>;

    /// @brief Common double-precision vector aliases.
    using Vector2D = Vector2<F64>;
    using Vector3D = Vector3<F64>;
    using Vector4D = Vector4<F64>;
}// namespace NGIN::Math

#pragma once

/// @file Transform.hpp
/// @brief Two- and three-dimensional translation, rotation, and scale transforms.

#include <NGIN/Math/AffineMatrix.hpp>
#include <NGIN/Math/Quaternion.hpp>

#include <cmath>
#include <concepts>

namespace NGIN::Math
{
    /// @brief A two-dimensional translation, rotation, and scale value.
    template<std::floating_point T>
    class Transform2
    {
    public:
        constexpr Transform2() = default;

        constexpr Transform2(Vector2<T> translation, T rotationRadians, Vector2<T> scale = Vector2<T> {T {1}, T {1}})
            : m_translation(translation), m_rotation(rotationRadians), m_scale(scale)
        {
        }

        [[nodiscard]] static constexpr Transform2 Identity() noexcept { return {}; }

        [[nodiscard]] constexpr Vector2<T>&       Translation() noexcept { return m_translation; }
        [[nodiscard]] constexpr const Vector2<T>& Translation() const noexcept { return m_translation; }
        [[nodiscard]] constexpr T&                Rotation() noexcept { return m_rotation; }
        [[nodiscard]] constexpr const T&          Rotation() const noexcept { return m_rotation; }
        [[nodiscard]] constexpr Vector2<T>&       Scale() noexcept { return m_scale; }
        [[nodiscard]] constexpr const Vector2<T>& Scale() const noexcept { return m_scale; }

        [[nodiscard]] bool operator==(const Transform2&) const = default;

    private:
        Vector2<T> m_translation {};
        T          m_rotation {0};
        Vector2<T> m_scale {T {1}, T {1}};
    };

    /// @brief A three-dimensional translation, rotation, and scale value.
    template<std::floating_point T>
    class Transform3
    {
    public:
        constexpr Transform3() = default;

        constexpr Transform3(
                Vector3<T>    translation,
                Quaternion<T> rotation,
                Vector3<T>    scale = Vector3<T> {T {1}, T {1}, T {1}})
            : m_translation(translation), m_rotation(rotation), m_scale(scale)
        {
        }

        [[nodiscard]] static constexpr Transform3 Identity() noexcept { return {}; }

        [[nodiscard]] constexpr Vector3<T>&          Translation() noexcept { return m_translation; }
        [[nodiscard]] constexpr const Vector3<T>&    Translation() const noexcept { return m_translation; }
        [[nodiscard]] constexpr Quaternion<T>&       Rotation() noexcept { return m_rotation; }
        [[nodiscard]] constexpr const Quaternion<T>& Rotation() const noexcept { return m_rotation; }
        [[nodiscard]] constexpr Vector3<T>&          Scale() noexcept { return m_scale; }
        [[nodiscard]] constexpr const Vector3<T>&    Scale() const noexcept { return m_scale; }

        [[nodiscard]] bool operator==(const Transform3&) const = default;

    private:
        Vector3<T>    m_translation {};
        Quaternion<T> m_rotation {};
        Vector3<T>    m_scale {T {1}, T {1}, T {1}};
    };

    template<std::floating_point T>
    [[nodiscard]] Matrix3<T> ToMatrix3(const Transform2<T>& transform)
    {
        const T cosine = std::cos(transform.Rotation());
        const T sine   = std::sin(transform.Rotation());
        return Matrix3<T> {
                cosine * transform.Scale().X(),
                -sine * transform.Scale().Y(),
                transform.Translation().X(),
                sine * transform.Scale().X(),
                cosine * transform.Scale().Y(),
                transform.Translation().Y(),
                T {0},
                T {0},
                T {1},
        };
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr AffineMatrix3<T> ToAffineMatrix3(const Transform3<T>& transform)
    {
        Matrix3<T> linear = ToMatrix3(transform.Rotation());
        for (std::size_t row = 0; row < 3; ++row)
        {
            linear(row, 0) *= transform.Scale().X();
            linear(row, 1) *= transform.Scale().Y();
            linear(row, 2) *= transform.Scale().Z();
        }
        return {linear, transform.Translation()};
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Matrix4<T> ToMatrix4(const Transform3<T>& transform)
    {
        return ToMatrix4(ToAffineMatrix3(transform));
    }

    template<std::floating_point T>
    [[nodiscard]] Vector2<T> TransformPoint(const Transform2<T>& transform, const Vector2<T>& point)
    {
        const Vector2<T> scaled = HadamardProduct(point, transform.Scale());
        const T          cosine = std::cos(transform.Rotation());
        const T          sine   = std::sin(transform.Rotation());
        return Vector2<T> {
                cosine * scaled.X() - sine * scaled.Y() + transform.Translation().X(),
                sine * scaled.X() + cosine * scaled.Y() + transform.Translation().Y(),
        };
    }

    template<std::floating_point T>
    [[nodiscard]] Vector2<T> TransformDirection(const Transform2<T>& transform, const Vector2<T>& direction)
    {
        const Vector2<T> scaled = HadamardProduct(direction, transform.Scale());
        const T          cosine = std::cos(transform.Rotation());
        const T          sine   = std::sin(transform.Rotation());
        return Vector2<T> {cosine * scaled.X() - sine * scaled.Y(), sine * scaled.X() + cosine * scaled.Y()};
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Vector3<T> TransformPoint(const Transform3<T>& transform, const Vector3<T>& point)
    {
        return Rotate(transform.Rotation(), HadamardProduct(point, transform.Scale())) + transform.Translation();
    }

    template<std::floating_point T>
    [[nodiscard]] constexpr Vector3<T> TransformDirection(const Transform3<T>& transform, const Vector3<T>& direction)
    {
        return Rotate(transform.Rotation(), HadamardProduct(direction, transform.Scale()));
    }

    /// @brief Composes 2D transforms without discarding shear introduced by non-uniform scale.
    template<std::floating_point T>
    [[nodiscard]] Matrix3<T> Compose(const Transform2<T>& parent, const Transform2<T>& local)
    {
        return ToMatrix3(parent) * ToMatrix3(local);
    }

    /// @brief Composes 3D transforms without discarding shear introduced by non-uniform scale.
    template<std::floating_point T>
    [[nodiscard]] constexpr AffineMatrix3<T> Compose(const Transform3<T>& parent, const Transform3<T>& local)
    {
        return ToAffineMatrix3(parent) * ToAffineMatrix3(local);
    }

    using Transform2F = Transform2<F32>;
    using Transform2D = Transform2<F64>;
    using Transform3F = Transform3<F32>;
    using Transform3D = Transform3<F64>;
}// namespace NGIN::Math

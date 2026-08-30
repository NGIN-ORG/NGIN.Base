#pragma once

/// @file Geometry.hpp
/// @brief Allocation-free geometric primitives and frustum tests.

#include <NGIN/Math/Projection.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>

namespace NGIN::Math
{
    template<std::floating_point T, std::size_t Dimension>
    class Ray
    {
    public:
        constexpr Ray() = default;
        constexpr Ray(Vector<T, Dimension> origin, Vector<T, Dimension> direction)
            : m_origin(origin), m_direction(direction)
        {
        }

        [[nodiscard]] constexpr Vector<T, Dimension>&       Origin() noexcept { return m_origin; }
        [[nodiscard]] constexpr const Vector<T, Dimension>& Origin() const noexcept { return m_origin; }
        [[nodiscard]] constexpr Vector<T, Dimension>&       Direction() noexcept { return m_direction; }
        [[nodiscard]] constexpr const Vector<T, Dimension>& Direction() const noexcept { return m_direction; }

        [[nodiscard]] constexpr Vector<T, Dimension> PointAt(T distance) const
        {
            return m_origin + m_direction * distance;
        }

        [[nodiscard]] constexpr bool operator==(const Ray&) const = default;

    private:
        Vector<T, Dimension> m_origin {};
        Vector<T, Dimension> m_direction {};
    };

    /// @brief A 3D plane with equation `Dot(normal, point) + distance = 0`.
    template<std::floating_point T>
    class Plane
    {
    public:
        constexpr Plane() = default;
        constexpr Plane(Vector3<T> normal, T distance) : m_normal(normal), m_distance(distance) {}

        [[nodiscard]] static constexpr Plane FromPointNormal(const Vector3<T>& point, const Vector3<T>& normal)
        {
            return {normal, -Dot(normal, point)};
        }

        [[nodiscard]] constexpr Vector3<T>&       Normal() noexcept { return m_normal; }
        [[nodiscard]] constexpr const Vector3<T>& Normal() const noexcept { return m_normal; }
        [[nodiscard]] constexpr T&                Distance() noexcept { return m_distance; }
        [[nodiscard]] constexpr const T&          Distance() const noexcept { return m_distance; }

        [[nodiscard]] constexpr T SignedDistance(const Vector3<T>& point) const
        {
            return Dot(m_normal, point) + m_distance;
        }

        [[nodiscard]] constexpr bool operator==(const Plane&) const = default;

    private:
        Vector3<T> m_normal {};
        T          m_distance {0};
    };

    template<std::floating_point T>
    [[nodiscard]] Plane<T> Normalize(const Plane<T>& plane)
    {
        const T length = Math::Length(plane.Normal());
        return {plane.Normal() / length, plane.Distance() / length};
    }

    template<std::floating_point T>
    [[nodiscard]] std::optional<Plane<T>> TryNormalize(const Plane<T>& plane, T tolerance = T {0})
    {
        const T lengthSquared     = LengthSquared(plane.Normal());
        const T absoluteTolerance = std::abs(tolerance);
        if (lengthSquared <= absoluteTolerance * absoluteTolerance)
            return std::nullopt;
        const T length = std::sqrt(lengthSquared);
        return Plane<T> {plane.Normal() / length, plane.Distance() / length};
    }

    template<std::floating_point T>
    [[nodiscard]] std::optional<Plane<T>> TryPlaneFromTriangle(
            const Vector3<T>& first,
            const Vector3<T>& second,
            const Vector3<T>& third,
            T                 tolerance = T {0})
    {
        const std::optional<Vector3<T>> normal = TryNormalize(Cross(second - first, third - first), tolerance);
        if (!normal)
            return std::nullopt;
        return Plane<T>::FromPointNormal(first, *normal);
    }

    /// @brief Returns the non-negative ray parameter at a plane intersection.
    template<std::floating_point T>
    [[nodiscard]] std::optional<T> Intersect(
            const Ray<T, 3>& ray,
            const Plane<T>&  plane,
            T                tolerance = T {0})
    {
        const T denominator = Dot(plane.Normal(), ray.Direction());
        if (std::abs(denominator) <= std::abs(tolerance))
            return std::nullopt;
        const T distance = -plane.SignedDistance(ray.Origin()) / denominator;
        if (distance < T {0})
            return std::nullopt;
        return distance;
    }

    template<std::floating_point T, std::size_t Dimension>
    class AABB
    {
    public:
        constexpr AABB()
            : m_minimum(Vector<T, Dimension>::Filled(std::numeric_limits<T>::max())),
              m_maximum(Vector<T, Dimension>::Filled(std::numeric_limits<T>::lowest()))
        {
        }

        constexpr AABB(Vector<T, Dimension> minimum, Vector<T, Dimension> maximum)
            : m_minimum(minimum), m_maximum(maximum)
        {
        }

        [[nodiscard]] constexpr Vector<T, Dimension>&       Minimum() noexcept { return m_minimum; }
        [[nodiscard]] constexpr const Vector<T, Dimension>& Minimum() const noexcept { return m_minimum; }
        [[nodiscard]] constexpr Vector<T, Dimension>&       Maximum() noexcept { return m_maximum; }
        [[nodiscard]] constexpr const Vector<T, Dimension>& Maximum() const noexcept { return m_maximum; }

        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            for (std::size_t index = 0; index < Dimension; ++index)
                if (m_minimum[index] > m_maximum[index])
                    return true;
            return false;
        }

        [[nodiscard]] constexpr Vector<T, Dimension> Center() const
        {
            return (m_minimum + m_maximum) / T {2};
        }

        [[nodiscard]] constexpr Vector<T, Dimension> Extents() const
        {
            return (m_maximum - m_minimum) / T {2};
        }

        constexpr void Expand(const Vector<T, Dimension>& point)
        {
            for (std::size_t index = 0; index < Dimension; ++index)
            {
                m_minimum[index] = std::min(m_minimum[index], point[index]);
                m_maximum[index] = std::max(m_maximum[index], point[index]);
            }
        }

        [[nodiscard]] constexpr bool Contains(const Vector<T, Dimension>& point) const
        {
            for (std::size_t index = 0; index < Dimension; ++index)
                if (point[index] < m_minimum[index] || point[index] > m_maximum[index])
                    return false;
            return !IsEmpty();
        }

        [[nodiscard]] constexpr bool Intersects(const AABB& other) const
        {
            if (IsEmpty() || other.IsEmpty())
                return false;
            for (std::size_t index = 0; index < Dimension; ++index)
                if (m_maximum[index] < other.m_minimum[index] || m_minimum[index] > other.m_maximum[index])
                    return false;
            return true;
        }

        [[nodiscard]] constexpr bool operator==(const AABB&) const = default;

    private:
        Vector<T, Dimension> m_minimum;
        Vector<T, Dimension> m_maximum;
    };

    template<std::floating_point T, std::size_t Dimension>
    class Sphere
    {
    public:
        constexpr Sphere() = default;
        constexpr Sphere(Vector<T, Dimension> center, T radius) : m_center(center), m_radius(radius) {}

        [[nodiscard]] constexpr Vector<T, Dimension>&       Center() noexcept { return m_center; }
        [[nodiscard]] constexpr const Vector<T, Dimension>& Center() const noexcept { return m_center; }
        [[nodiscard]] constexpr T&                          Radius() noexcept { return m_radius; }
        [[nodiscard]] constexpr const T&                    Radius() const noexcept { return m_radius; }

        [[nodiscard]] constexpr bool Contains(const Vector<T, Dimension>& point) const
        {
            return DistanceSquared(m_center, point) <= m_radius * m_radius;
        }

        [[nodiscard]] constexpr bool Intersects(const Sphere& other) const
        {
            const T combinedRadius = m_radius + other.m_radius;
            return DistanceSquared(m_center, other.m_center) <= combinedRadius * combinedRadius;
        }

        [[nodiscard]] constexpr bool operator==(const Sphere&) const = default;

    private:
        Vector<T, Dimension> m_center {};
        T                    m_radius {0};
    };

    /// @brief Returns the entering and exiting ray parameters for an axis-aligned box.
    template<std::floating_point T, std::size_t Dimension>
    [[nodiscard]] std::optional<Vector2<T>> Intersect(
            const Ray<T, Dimension>&  ray,
            const AABB<T, Dimension>& bounds,
            T                         tolerance = T {0})
    {
        if (bounds.IsEmpty())
            return std::nullopt;

        T       nearDistance      = T {0};
        T       farDistance       = std::numeric_limits<T>::infinity();
        const T absoluteTolerance = std::abs(tolerance);
        for (std::size_t index = 0; index < Dimension; ++index)
        {
            if (std::abs(ray.Direction()[index]) <= absoluteTolerance)
            {
                if (ray.Origin()[index] < bounds.Minimum()[index] || ray.Origin()[index] > bounds.Maximum()[index])
                    return std::nullopt;
                continue;
            }

            const T reciprocalDirection = T {1} / ray.Direction()[index];
            T       firstDistance       = (bounds.Minimum()[index] - ray.Origin()[index]) * reciprocalDirection;
            T       secondDistance      = (bounds.Maximum()[index] - ray.Origin()[index]) * reciprocalDirection;
            if (firstDistance > secondDistance)
                std::swap(firstDistance, secondDistance);
            nearDistance = std::max(nearDistance, firstDistance);
            farDistance  = std::min(farDistance, secondDistance);
            if (nearDistance > farDistance)
                return std::nullopt;
        }
        return Vector2<T> {nearDistance, farDistance};
    }

    /// @brief Returns the nearest non-negative ray parameter at a sphere intersection.
    template<std::floating_point T, std::size_t Dimension>
    [[nodiscard]] std::optional<T> Intersect(
            const Ray<T, Dimension>&    ray,
            const Sphere<T, Dimension>& sphere,
            T                           tolerance = T {0})
    {
        const Vector<T, Dimension> offset                 = ray.Origin() - sphere.Center();
        const T                    directionLengthSquared = LengthSquared(ray.Direction());
        const T                    absoluteTolerance      = std::abs(tolerance);
        if (directionLengthSquared <= absoluteTolerance * absoluteTolerance)
            return std::nullopt;

        const T halfLinear   = Dot(offset, ray.Direction());
        const T constant     = LengthSquared(offset) - sphere.Radius() * sphere.Radius();
        const T discriminant = halfLinear * halfLinear - directionLengthSquared * constant;
        if (discriminant < T {0})
            return std::nullopt;

        const T root          = std::sqrt(discriminant);
        const T firstDistance = (-halfLinear - root) / directionLengthSquared;
        if (firstDistance >= T {0})
            return firstDistance;
        const T secondDistance = (-halfLinear + root) / directionLengthSquared;
        if (secondDistance >= T {0})
            return secondDistance;
        return std::nullopt;
    }

    enum class FrustumPlane : std::size_t
    {
        Left,
        Right,
        Bottom,
        Top,
        Near,
        Far,
        Count,
    };

    template<std::floating_point T>
    class Frustum
    {
    public:
        using PlaneArray = std::array<Plane<T>, static_cast<std::size_t>(FrustumPlane::Count)>;

        constexpr Frustum() = default;
        constexpr explicit Frustum(PlaneArray planes) : m_planes(planes) {}

        [[nodiscard]] constexpr Plane<T>& operator[](FrustumPlane plane) noexcept
        {
            return m_planes[static_cast<std::size_t>(plane)];
        }

        [[nodiscard]] constexpr const Plane<T>& operator[](FrustumPlane plane) const noexcept
        {
            return m_planes[static_cast<std::size_t>(plane)];
        }

        [[nodiscard]] constexpr const PlaneArray& Planes() const noexcept { return m_planes; }

        [[nodiscard]] constexpr bool Contains(const Vector3<T>& point) const
        {
            for (const Plane<T>& plane: m_planes)
                if (plane.SignedDistance(point) < T {0})
                    return false;
            return true;
        }

        [[nodiscard]] constexpr bool Intersects(const Sphere<T, 3>& sphere) const
        {
            for (const Plane<T>& plane: m_planes)
                if (plane.SignedDistance(sphere.Center()) < -sphere.Radius())
                    return false;
            return true;
        }

        [[nodiscard]] constexpr bool Intersects(const AABB<T, 3>& bounds) const
        {
            if (bounds.IsEmpty())
                return false;
            for (const Plane<T>& plane: m_planes)
            {
                Vector3<T> positive;
                for (std::size_t index = 0; index < 3; ++index)
                    positive[index] = plane.Normal()[index] >= T {0} ? bounds.Maximum()[index] : bounds.Minimum()[index];
                if (plane.SignedDistance(positive) < T {0})
                    return false;
            }
            return true;
        }

    private:
        PlaneArray m_planes {};
    };

    /// @brief Extracts normalized inward-facing planes from a column-vector view-projection matrix.
    template<std::floating_point T>
    [[nodiscard]] std::optional<Frustum<T>> TryExtractFrustum(
            const Matrix4<T>& viewProjection,
            ClipDepthRange    depthRange,
            T                 tolerance = T {0})
    {
        const auto rowPlane = [&viewProjection](std::size_t firstRow, T firstScale, std::size_t secondRow, T secondScale) {
            return Plane<T> {
                    Vector3<T> {
                            firstScale * viewProjection(firstRow, 0) + secondScale * viewProjection(secondRow, 0),
                            firstScale * viewProjection(firstRow, 1) + secondScale * viewProjection(secondRow, 1),
                            firstScale * viewProjection(firstRow, 2) + secondScale * viewProjection(secondRow, 2),
                    },
                    firstScale * viewProjection(firstRow, 3) + secondScale * viewProjection(secondRow, 3),
            };
        };

        typename Frustum<T>::PlaneArray planes {
                rowPlane(3, T {1}, 0, T {1}),
                rowPlane(3, T {1}, 0, T {-1}),
                rowPlane(3, T {1}, 1, T {1}),
                rowPlane(3, T {1}, 1, T {-1}),
                depthRange == ClipDepthRange::ZeroToOne
                        ? rowPlane(2, T {1}, 2, T {0})
                        : rowPlane(3, T {1}, 2, T {1}),
                rowPlane(3, T {1}, 2, T {-1}),
        };

        for (Plane<T>& plane: planes)
        {
            const std::optional<Plane<T>> normalized = TryNormalize(plane, tolerance);
            if (!normalized)
                return std::nullopt;
            plane = *normalized;
        }
        return Frustum<T> {planes};
    }

    template<std::floating_point T>
    using Ray2 = Ray<T, 2>;
    template<std::floating_point T>
    using Ray3 = Ray<T, 3>;
    template<std::floating_point T>
    using AABB2 = AABB<T, 2>;
    template<std::floating_point T>
    using AABB3 = AABB<T, 3>;
    template<std::floating_point T>
    using Sphere2 = Sphere<T, 2>;
    template<std::floating_point T>
    using Sphere3 = Sphere<T, 3>;

    using Ray2F    = Ray2<F32>;
    using Ray3F    = Ray3<F32>;
    using Ray2D    = Ray2<F64>;
    using Ray3D    = Ray3<F64>;
    using PlaneF   = Plane<F32>;
    using PlaneD   = Plane<F64>;
    using AABB2F   = AABB2<F32>;
    using AABB3F   = AABB3<F32>;
    using AABB2D   = AABB2<F64>;
    using AABB3D   = AABB3<F64>;
    using Sphere2F = Sphere2<F32>;
    using Sphere3F = Sphere3<F32>;
    using Sphere2D = Sphere2<F64>;
    using Sphere3D = Sphere3<F64>;
    using FrustumF = Frustum<F32>;
    using FrustumD = Frustum<F64>;
}// namespace NGIN::Math

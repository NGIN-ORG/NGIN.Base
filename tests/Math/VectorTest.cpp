/// @file VectorTest.cpp
/// @brief Tests for fixed-size mathematical vectors.

#include <NGIN/Math/Vector.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <type_traits>

using namespace NGIN::Math;

namespace
{
    constexpr Vector3<int> LEFT {1, 2, 3};
    constexpr Vector3<int> RIGHT {4, 5, 6};

    static_assert((LEFT + RIGHT) == Vector3<int> {5, 7, 9});
    static_assert((RIGHT - LEFT) == Vector3<int> {3, 3, 3});
    static_assert(Dot(LEFT, RIGHT) == 32);
    static_assert(LengthSquared(LEFT) == 14);
    static_assert(DistanceSquared(LEFT, RIGHT) == 27);
    static_assert(Cross(LEFT, RIGHT) == Vector3<int> {-3, 6, -3});
    static_assert(HadamardProduct(LEFT, RIGHT) == Vector3<int> {4, 10, 18});
    static_assert(Vector4F::Size() == 4);
    static_assert(std::is_trivially_copyable_v<Vector3F>);
}// namespace

TEST_CASE("Math vectors expose contiguous components and named accessors", "[Math][Vector]")
{
    Vector4F value {1.0F, 2.0F, 3.0F, 4.0F};
    CHECK(value.X() == 1.0F);
    CHECK(value.Y() == 2.0F);
    CHECK(value.Z() == 3.0F);
    CHECK(value.W() == 4.0F);

    value.Y() = 8.0F;
    CHECK(value[1] == 8.0F);
    CHECK(value.Data()[3] == 4.0F);

    const std::array<float, 4> components {value[0], value[1], value[2], value[3]};
    CHECK(components == std::array {1.0F, 8.0F, 3.0F, 4.0F});
}

TEST_CASE("Math vectors support scalar and component-wise arithmetic", "[Math][Vector]")
{
    const Vector3D value {2.0, -4.0, 8.0};
    CHECK(value * 0.5 == Vector3D {1.0, -2.0, 4.0});
    CHECK(2.0 * value == Vector3D {4.0, -8.0, 16.0});
    CHECK(value / 2.0 == Vector3D {1.0, -2.0, 4.0});
    CHECK(-value == Vector3D {-2.0, 4.0, -8.0});
    CHECK(HadamardProduct(value, Vector3D {3.0, 2.0, 0.5}) == Vector3D {6.0, -8.0, 4.0});

    const Vector3D converted(Vector3<int> {1, 2, 3});
    CHECK(converted == Vector3D {1.0, 2.0, 3.0});
    CHECK(Vector3D::Filled(7.0) == Vector3D {7.0, 7.0, 7.0});
}

TEST_CASE("Math vectors compute Euclidean geometry", "[Math][Vector]")
{
    const Vector2D value {3.0, 4.0};
    CHECK(Length(value) == Catch::Approx(5.0));
    CHECK(Distance(value, Vector2D {0.0, 0.0}) == Catch::Approx(5.0));

    const Vector2D fastNormalized = Normalize(value);
    CHECK(fastNormalized.X() == Catch::Approx(0.6));
    CHECK(fastNormalized.Y() == Catch::Approx(0.8));

    const auto normalized = TryNormalize(value);
    REQUIRE(normalized.has_value());
    CHECK(normalized->X() == Catch::Approx(0.6));
    CHECK(normalized->Y() == Catch::Approx(0.8));
    CHECK(Length(*normalized) == Catch::Approx(1.0));

    const Vector3D cross = Cross(Vector3D {1.0, 0.0, 0.0}, Vector3D {0.0, 1.0, 0.0});
    CHECK(cross == Vector3D {0.0, 0.0, 1.0});
    CHECK(Dot(cross, Vector3D {1.0, 0.0, 0.0}) == 0.0);
    CHECK(Dot(cross, Vector3D {0.0, 1.0, 0.0}) == 0.0);
}

TEST_CASE("Math vector normalization rejects degenerate inputs", "[Math][Vector]")
{
    CHECK_FALSE(TryNormalize(Vector3D {}).has_value());
    CHECK_FALSE(TryNormalize(Vector2D {0.001, 0.0}, 0.01).has_value());
    CHECK_FALSE(TryNormalize(Vector2D {0.001, 0.0}, -0.01).has_value());
}

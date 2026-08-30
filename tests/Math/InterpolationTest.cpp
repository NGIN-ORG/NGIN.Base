/// @file InterpolationTest.cpp
/// @brief Tests for scalar, vector, quaternion, and transform interpolation.

#include <NGIN/Math/Interpolation.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

using namespace NGIN::Math;

TEST_CASE("Math linear and smooth interpolation support scalar and vector values", "[Math][Interpolation]")
{
    CHECK(Lerp(2.0, 6.0, 0.25) == 3.0);
    CHECK(Lerp(Vector3D {0.0, 2.0, 4.0}, Vector3D {2.0, 4.0, 6.0}, 0.5) == Vector3D {1.0, 3.0, 5.0});
    CHECK(SmoothStep(0.0, 10.0, -1.0) == 0.0);
    CHECK(SmoothStep(0.0, 10.0, 0.5) == 5.0);
    CHECK(SmoothStep(0.0, 10.0, 2.0) == 10.0);
}

TEST_CASE("Math quaternion interpolation follows the shortest rotation arc", "[Math][Interpolation]")
{
    const QuaternionD start   = QuaternionD::Identity();
    const QuaternionD end     = QuaternionFromAxisAngle(Vector3D {0.0, 0.0, 1.0}, std::numbers::pi);
    const QuaternionD halfway = Slerp(start, end, 0.5);
    const Vector3D    rotated = Rotate(halfway, Vector3D {1.0, 0.0, 0.0});
    CHECK(rotated.X() == Catch::Approx(0.0).margin(1e-12));
    CHECK(rotated.Y() == Catch::Approx(1.0).margin(1e-12));
    CHECK(Length(halfway) == Catch::Approx(1.0));

    const QuaternionD sameRotation = -end;
    CHECK(std::abs(Dot(Slerp(end, sameRotation, 0.5), end)) == Catch::Approx(1.0));
}

TEST_CASE("Math transform interpolation combines translation, rotation, and scale", "[Math][Interpolation]")
{
    const Transform3D start;
    const Transform3D end {
            Vector3D {10.0, 4.0, -2.0},
            QuaternionFromAxisAngle(Vector3D {0.0, 1.0, 0.0}, std::numbers::pi),
            Vector3D {3.0, 5.0, 7.0},
    };
    const Transform3D halfway = Interpolate(start, end, 0.5);
    CHECK(halfway.Translation() == Vector3D {5.0, 2.0, -1.0});
    CHECK(halfway.Scale() == Vector3D {2.0, 3.0, 4.0});
    CHECK(Length(halfway.Rotation()) == Catch::Approx(1.0));

    const Transform2D angleStart {Vector2D {}, std::numbers::pi * 0.9};
    const Transform2D angleEnd {Vector2D {}, -std::numbers::pi * 0.9};
    const Transform2D angleHalfway = Interpolate(angleStart, angleEnd, 0.5);
    CHECK(std::abs(angleHalfway.Rotation()) == Catch::Approx(std::numbers::pi));
}

/// @file QuaternionTest.cpp
/// @brief Tests for three-dimensional rotation quaternions.

#include <NGIN/Math/Quaternion.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

using namespace NGIN::Math;

namespace
{
    void CheckVector(const Vector3D& value, const Vector3D& expected, double margin = 1e-12)
    {
        CHECK(value.X() == Catch::Approx(expected.X()).margin(margin));
        CHECK(value.Y() == Catch::Approx(expected.Y()).margin(margin));
        CHECK(value.Z() == Catch::Approx(expected.Z()).margin(margin));
    }

    constexpr QuaternionD IDENTITY;
    static_assert(IDENTITY == QuaternionD {0.0, 0.0, 0.0, 1.0});
    static_assert(Conjugate(QuaternionD {1.0, 2.0, 3.0, 4.0}) == QuaternionD {-1.0, -2.0, -3.0, 4.0});
}// namespace

TEST_CASE("Math quaternions rotate column vectors and convert to matrices", "[Math][Quaternion]")
{
    const QuaternionD rotation = QuaternionFromAxisAngle(Vector3D {0.0, 0.0, 1.0}, std::numbers::pi / 2.0);
    CheckVector(Rotate(rotation, Vector3D {1.0, 0.0, 0.0}), Vector3D {0.0, 1.0, 0.0});
    CheckVector(ToMatrix3(rotation) * Vector3D {1.0, 0.0, 0.0}, Vector3D {0.0, 1.0, 0.0});

    const QuaternionD roundTrip = QuaternionFromRotationMatrix(ToMatrix3(rotation));
    CHECK(std::abs(Dot(rotation, roundTrip)) == Catch::Approx(1.0).margin(1e-12));
}

TEST_CASE("Math quaternion multiplication composes parent and local rotations", "[Math][Quaternion]")
{
    const QuaternionD parent = QuaternionFromAxisAngle(Vector3D {0.0, 0.0, 1.0}, std::numbers::pi / 2.0);
    const QuaternionD local  = QuaternionFromAxisAngle(Vector3D {1.0, 0.0, 0.0}, std::numbers::pi / 2.0);
    const Vector3D    input {0.0, 1.0, 0.0};

    CheckVector(Rotate(parent * local, input), Rotate(parent, Rotate(local, input)));
    CheckVector(Rotate(Inverse(parent), Rotate(parent, input)), input);
}

TEST_CASE("Math quaternion matrix conversion handles half turns around every axis", "[Math][Quaternion]")
{
    for (const Vector3D& axis: {Vector3D {1.0, 0.0, 0.0}, Vector3D {0.0, 1.0, 0.0}, Vector3D {0.0, 0.0, 1.0}})
    {
        const QuaternionD rotation  = QuaternionFromAxisAngle(axis, std::numbers::pi);
        const QuaternionD roundTrip = QuaternionFromRotationMatrix(ToMatrix3(rotation));
        CHECK(std::abs(Dot(rotation, roundTrip)) == Catch::Approx(1.0).margin(1e-12));
    }
}

TEST_CASE("Math quaternion checked operations reject degenerate input", "[Math][Quaternion]")
{
    CHECK_FALSE(TryNormalize(QuaternionD {0.0, 0.0, 0.0, 0.0}).has_value());
    CHECK_FALSE(TryInverse(QuaternionD {0.0, 0.0, 0.0, 0.0}).has_value());
    CHECK_FALSE(TryQuaternionFromAxisAngle(Vector3D {}, 1.0).has_value());

    const auto rotation = TryQuaternionFromAxisAngle(Vector3D {0.0, 2.0, 0.0}, 0.5);
    REQUIRE(rotation.has_value());
    CHECK(Length(*rotation) == Catch::Approx(1.0));
}

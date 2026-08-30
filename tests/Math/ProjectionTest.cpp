/// @file ProjectionTest.cpp
/// @brief Tests for explicit view and projection matrix conventions.

#include <NGIN/Math/Projection.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

using namespace NGIN::Math;

namespace
{
    double ProjectDepth(const Matrix4D& projection, double viewSpaceDepth)
    {
        const Vector4D clip = projection * Vector4D {0.0, 0.0, viewSpaceDepth, 1.0};
        return clip.Z() / clip.W();
    }
}// namespace

TEST_CASE("Math perspective matrices map explicit handedness and clip depth", "[Math][Projection]")
{
    constexpr double nearPlane = 0.5;
    constexpr double farPlane  = 100.0;
    for (const Handedness handedness: {Handedness::Right, Handedness::Left})
    {
        const double   direction = handedness == Handedness::Right ? -1.0 : 1.0;
        const Matrix4D zeroToOne = Perspective(
                std::numbers::pi / 2.0,
                16.0 / 9.0,
                nearPlane,
                farPlane,
                handedness,
                ClipDepthRange::ZeroToOne);
        CHECK(ProjectDepth(zeroToOne, direction * nearPlane) == Catch::Approx(0.0).margin(1e-12));
        CHECK(ProjectDepth(zeroToOne, direction * farPlane) == Catch::Approx(1.0).margin(1e-12));

        const Matrix4D negativeOneToOne = Perspective(
                std::numbers::pi / 2.0,
                1.0,
                nearPlane,
                farPlane,
                handedness,
                ClipDepthRange::NegativeOneToOne);
        CHECK(ProjectDepth(negativeOneToOne, direction * nearPlane) == Catch::Approx(-1.0).margin(1e-12));
        CHECK(ProjectDepth(negativeOneToOne, direction * farPlane) == Catch::Approx(1.0).margin(1e-12));
    }
}

TEST_CASE("Math orthographic matrices map their declared volume", "[Math][Projection]")
{
    const Matrix4D projection = Orthographic(
            -4.0,
            6.0,
            -2.0,
            8.0,
            1.0,
            11.0,
            Handedness::Right,
            ClipDepthRange::ZeroToOne);
    const Vector4D minimum = projection * Vector4D {-4.0, -2.0, -1.0, 1.0};
    const Vector4D maximum = projection * Vector4D {6.0, 8.0, -11.0, 1.0};
    for (std::size_t index = 0; index < 4; ++index)
    {
        CHECK(minimum[index] == Catch::Approx(Vector4D {-1.0, -1.0, 0.0, 1.0}[index]).margin(1e-12));
        CHECK(maximum[index] == Catch::Approx(Vector4D {1.0, 1.0, 1.0, 1.0}[index]).margin(1e-12));
    }
}

TEST_CASE("Math view matrices place the eye at the origin", "[Math][Projection]")
{
    const Vector3D eye {2.0, 3.0, 4.0};
    const Vector3D target {2.0, 3.0, 3.0};
    const Matrix4D rightHanded  = LookAt(eye, target, Vector3D {0.0, 1.0, 0.0}, Handedness::Right);
    const Vector4D eyeInView    = rightHanded * Vector4D {eye.X(), eye.Y(), eye.Z(), 1.0};
    const Vector4D targetInView = rightHanded * Vector4D {target.X(), target.Y(), target.Z(), 1.0};
    CHECK(eyeInView == Vector4D {0.0, 0.0, 0.0, 1.0});
    CHECK(targetInView == Vector4D {0.0, 0.0, -1.0, 1.0});

    const Matrix4D leftHanded       = LookAt(eye, target, Vector3D {0.0, 1.0, 0.0}, Handedness::Left);
    const Vector4D leftTargetInView = leftHanded * Vector4D {target.X(), target.Y(), target.Z(), 1.0};
    CHECK(leftTargetInView == Vector4D {0.0, 0.0, 1.0, 1.0});

    CHECK_FALSE(TryLookAt(eye, eye, Vector3D {0.0, 1.0, 0.0}, Handedness::Right).has_value());
    CHECK_FALSE(TryLookAt(eye, target, Vector3D {0.0, 0.0, 1.0}, Handedness::Right).has_value());
}

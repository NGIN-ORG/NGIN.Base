/// @file TransformTest.cpp
/// @brief Tests for affine matrices, transforms, and decomposition.

#include <NGIN/Math/Decomposition.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

using namespace NGIN::Math;

namespace
{
    template<std::size_t Dimension>
    void CheckVector(const Vector<double, Dimension>& value, const Vector<double, Dimension>& expected, double margin = 1e-11)
    {
        for (std::size_t index = 0; index < Dimension; ++index)
            CHECK(value[index] == Catch::Approx(expected[index]).margin(margin));
    }

    template<std::size_t Size>
    void CheckMatrix(const Matrix<double, Size, Size>& value, const Matrix<double, Size, Size>& expected, double margin = 1e-11)
    {
        for (std::size_t row = 0; row < Size; ++row)
            for (std::size_t column = 0; column < Size; ++column)
                CHECK(value(row, column) == Catch::Approx(expected(row, column)).margin(margin));
    }

    static_assert(sizeof(AffineMatrix3F) == 12 * sizeof(float));
    static_assert(AffineMatrix3D::Identity().Data()[0] == 1.0);
    static_assert(Transform3D::Identity().Scale() == Vector3D {1.0, 1.0, 1.0});
}// namespace

TEST_CASE("Math 2D transforms match homogeneous column-vector matrices", "[Math][Transform]")
{
    const Transform2D transform {Vector2D {3.0, -2.0}, std::numbers::pi / 2.0, Vector2D {2.0, 4.0}};
    const Vector2D    point {1.0, 2.0};
    const Vector2D    transformed = TransformPoint(transform, point);
    CheckVector(transformed, Vector2D {-5.0, 0.0});

    const Vector3D homogeneous = ToMatrix3(transform) * Vector3D {point.X(), point.Y(), 1.0};
    CheckVector(homogeneous, Vector3D {transformed.X(), transformed.Y(), 1.0});
}

TEST_CASE("Math compact affine matrices transform and compose without a fourth row", "[Math][AffineMatrix]")
{
    const Transform3D parent {
            Vector3D {10.0, 0.0, 0.0},
            QuaternionFromAxisAngle(Vector3D {0.0, 0.0, 1.0}, std::numbers::pi / 2.0),
    };
    const Transform3D    local {Vector3D {2.0, 0.0, 0.0}, QuaternionD::Identity(), Vector3D {2.0, 2.0, 2.0}};
    const AffineMatrix3D composed = Compose(parent, local);

    CheckVector(TransformPoint(composed, Vector3D {1.0, 0.0, 0.0}), Vector3D {10.0, 4.0, 0.0});
    CheckVector(
            TransformPoint(composed, Vector3D {1.0, 0.0, 0.0}),
            TransformPoint(parent, TransformPoint(local, Vector3D {1.0, 0.0, 0.0})));

    const AffineMatrix3D inverse = Inverse(composed);
    CheckVector(TransformPoint(inverse, TransformPoint(composed, Vector3D {3.0, -2.0, 5.0})), Vector3D {3.0, -2.0, 5.0});
    CheckMatrix(ToMatrix4(composed) * ToMatrix4(inverse), Matrix4D::Identity());
}

TEST_CASE("Math affine conversion validates the implicit fourth row", "[Math][AffineMatrix]")
{
    const Matrix4D affine = ToMatrix4(AffineMatrix3D {Matrix3D::Identity(), Vector3D {1.0, 2.0, 3.0}});
    REQUIRE(TryToAffineMatrix3(affine).has_value());
    CHECK(ToMatrix4(*TryToAffineMatrix3(affine)) == affine);

    Matrix4D perspective = affine;
    perspective(3, 0)    = 0.1;
    CHECK_FALSE(TryToAffineMatrix3(perspective).has_value());
    CHECK_FALSE(TryInverse(AffineMatrix3D {Matrix3D::Filled(0.0), Vector3D {}}).has_value());
}

TEST_CASE("Math decomposition round-trips 2D and 3D TRS", "[Math][Decomposition]")
{
    const Transform2D transform2 {Vector2D {4.0, -7.0}, 0.7, Vector2D {-2.0, 3.0}};
    const auto        decomposed2 = TryDecompose(ToMatrix3(transform2));
    REQUIRE(decomposed2.has_value());
    CheckMatrix(ToMatrix3(*decomposed2), ToMatrix3(transform2));

    const Transform3D transform3 {
            Vector3D {4.0, -7.0, 2.0},
            QuaternionFromAxisAngle(Normalize(Vector3D {1.0, 2.0, 3.0}), 1.1),
            Vector3D {-2.0, 3.0, 0.5},
    };
    const auto decomposed3 = TryDecompose(ToAffineMatrix3(transform3));
    REQUIRE(decomposed3.has_value());
    CheckMatrix(ToMatrix4(*decomposed3), ToMatrix4(transform3), 1e-10);
}

TEST_CASE("Math decomposition rejects shear, perspective, and degenerate scale", "[Math][Decomposition]")
{
    Matrix3D shear2 = Matrix3D::Identity();
    shear2(0, 1)    = 0.25;
    CHECK_FALSE(TryDecompose(shear2).has_value());

    Matrix4D shear3 = Matrix4D::Identity();
    shear3(0, 1)    = 0.25;
    CHECK_FALSE(TryDecompose(shear3).has_value());

    Matrix4D perspective = Matrix4D::Identity();
    perspective(3, 2)    = -1.0;
    CHECK_FALSE(TryDecompose(perspective).has_value());

    Matrix4D degenerate = Matrix4D::Identity();
    degenerate(0, 0)    = 0.0;
    CHECK_FALSE(TryDecompose(degenerate).has_value());
}

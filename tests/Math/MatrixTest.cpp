/// @file MatrixTest.cpp
/// @brief Tests for fixed-size row-major matrices.

#include <NGIN/Math/Matrix.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace NGIN::Math;

namespace
{
    constexpr Matrix2<int> LEFT {1, 2, 3, 4};
    constexpr Matrix2<int> RIGHT {5, 6, 7, 8};
    constexpr Matrix2<int> PRODUCT = LEFT * RIGHT;

    static_assert(PRODUCT == Matrix2<int> {19, 22, 43, 50});
    static_assert(Transpose(LEFT) == Matrix2<int> {1, 3, 2, 4});
    static_assert(Trace(LEFT) == 5);
    static_assert(Determinant(LEFT) == -2);
    static_assert(Matrix3<int>::Identity() * Vector3<int> {2, 3, 4} == Vector3<int> {2, 3, 4});
}// namespace

TEST_CASE("Math matrices use explicit row-major storage", "[Math][Matrix]")
{
    Matrix<double, 2, 3> value {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    CHECK(value.Rows() == 2);
    CHECK(value.Columns() == 3);
    CHECK(value.Size() == 6);
    CHECK(value(1, 2) == 6.0);
    CHECK(value.Row(0) == Vector3D {1.0, 2.0, 3.0});
    CHECK(value.Column(1) == Vector2D {2.0, 5.0});

    value.SetRow(1, Vector3D {7.0, 8.0, 9.0});
    value.SetColumn(0, Vector2D {-1.0, -2.0});
    CHECK((value == Matrix<double, 2, 3> {-1.0, 2.0, 3.0, -2.0, 8.0, 9.0}));
    CHECK(value.Data()[4] == 8.0);
}

TEST_CASE("Math matrices compose compatible shapes", "[Math][Matrix]")
{
    const Matrix<double, 2, 3> left {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    const Matrix<double, 3, 2> right {7.0, 8.0, 9.0, 10.0, 11.0, 12.0};
    CHECK(left * right == Matrix2D {58.0, 64.0, 139.0, 154.0});
    CHECK(left * Vector3D {1.0, 0.0, -1.0} == Vector2D {-2.0, -2.0});
    CHECK((Transpose(left) == Matrix<double, 3, 2> {1.0, 4.0, 2.0, 5.0, 3.0, 6.0}));

    CHECK(left + left == left * 2.0);
    CHECK(3.0 * left - left == left * 2.0);
    CHECK((left / 2.0 == Matrix<double, 2, 3> {0.5, 1.0, 1.5, 2.0, 2.5, 3.0}));
}

TEST_CASE("Math matrix determinants handle pivoting and singularity", "[Math][Matrix]")
{
    const Matrix3<int> pivoted {0, 2, 1, 3, 0, 4, 5, 6, 0};
    CHECK(Determinant(pivoted) == 58);
    CHECK(Determinant(Matrix3<int> {1, 2, 3, 2, 4, 6, 7, 8, 9}) == 0);
    CHECK(Determinant(Matrix<int, 1, 1> {9}) == 9);
}

TEST_CASE("Math matrices provide checked inversion", "[Math][Matrix]")
{
    const Matrix3D value {3.0, 0.0, 2.0, 2.0, 0.0, -2.0, 0.0, 1.0, 1.0};
    const auto     inverse = TryInverse(value);
    REQUIRE(inverse.has_value());

    const Matrix3D identity = value * *inverse;
    for (std::size_t row = 0; row < identity.Rows(); ++row)
    {
        for (std::size_t column = 0; column < identity.Columns(); ++column)
        {
            const double expected = row == column ? 1.0 : 0.0;
            CHECK(identity(row, column) == Catch::Approx(expected).margin(1e-12));
        }
    }

    const Matrix2D needsPivot {0.0, 2.0, 1.0, 0.0};
    REQUIRE(TryInverse(needsPivot).has_value());
    CHECK(needsPivot * *TryInverse(needsPivot) == Matrix2D::Identity());
}

TEST_CASE("Math matrix inversion rejects singular and tolerance-degenerate inputs", "[Math][Matrix]")
{
    CHECK_FALSE(TryInverse(Matrix2D {1.0, 2.0, 2.0, 4.0}).has_value());
    CHECK_FALSE(TryInverse(Matrix2D {1e-8, 0.0, 0.0, 1.0}, 1e-6).has_value());
    CHECK(TryInverse(Matrix2D {1e-8, 0.0, 0.0, 1.0}).has_value());
}

/// @file GeometryTest.cpp
/// @brief Tests for rays, planes, bounds, spheres, and frusta.

#include <NGIN/Math/Geometry.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

using namespace NGIN::Math;

TEST_CASE("Math rays and planes expose fundamental geometric operations", "[Math][Geometry]")
{
    const Ray3D ray {Vector3D {1.0, 2.0, 3.0}, Vector3D {0.0, 0.0, -2.0}};
    CHECK(ray.PointAt(1.5) == Vector3D {1.0, 2.0, 0.0});

    const PlaneD plane = PlaneD::FromPointNormal(Vector3D {0.0, 5.0, 0.0}, Vector3D {0.0, 1.0, 0.0});
    CHECK(plane.SignedDistance(Vector3D {0.0, 8.0, 0.0}) == 3.0);
    const auto planeHit = Intersect(ray, PlaneD::FromPointNormal(Vector3D {0.0, 0.0, 0.0}, Vector3D {0.0, 0.0, 1.0}));
    REQUIRE(planeHit.has_value());
    CHECK(*planeHit == Catch::Approx(1.5));

    const auto triangle = TryPlaneFromTriangle(
            Vector3D {0.0, 0.0, 0.0},
            Vector3D {1.0, 0.0, 0.0},
            Vector3D {0.0, 1.0, 0.0});
    REQUIRE(triangle.has_value());
    CHECK(triangle->Normal() == Vector3D {0.0, 0.0, 1.0});
    CHECK_FALSE(TryPlaneFromTriangle(Vector3D {}, Vector3D {1.0, 0.0, 0.0}, Vector3D {2.0, 0.0, 0.0}).has_value());
}

TEST_CASE("Math axis-aligned bounds expand, contain, and intersect", "[Math][Geometry]")
{
    AABB3D bounds;
    CHECK(bounds.IsEmpty());
    bounds.Expand(Vector3D {2.0, -1.0, 4.0});
    bounds.Expand(Vector3D {-2.0, 3.0, 0.0});
    CHECK(bounds.Minimum() == Vector3D {-2.0, -1.0, 0.0});
    CHECK(bounds.Maximum() == Vector3D {2.0, 3.0, 4.0});
    CHECK(bounds.Center() == Vector3D {0.0, 1.0, 2.0});
    CHECK(bounds.Contains(Vector3D {0.0, 0.0, 1.0}));
    CHECK(bounds.Intersects(AABB3D {Vector3D {1.0, 2.0, 3.0}, Vector3D {5.0, 6.0, 7.0}}));
    CHECK_FALSE(bounds.Intersects(AABB3D {Vector3D {3.0, 4.0, 5.0}, Vector3D {5.0, 6.0, 7.0}}));

    const auto hit = Intersect(Ray3D {Vector3D {-5.0, 1.0, 2.0}, Vector3D {1.0, 0.0, 0.0}}, bounds);
    REQUIRE(hit.has_value());
    CHECK(hit->X() == 3.0);
    CHECK(hit->Y() == 7.0);
    CHECK_FALSE(Intersect(Ray3D {Vector3D {-5.0, 8.0, 2.0}, Vector3D {1.0, 0.0, 0.0}}, bounds).has_value());
}

TEST_CASE("Math spheres contain points and intersect other spheres", "[Math][Geometry]")
{
    const Sphere3D sphere {Vector3D {1.0, 2.0, 3.0}, 2.0};
    CHECK(sphere.Contains(Vector3D {3.0, 2.0, 3.0}));
    CHECK_FALSE(sphere.Contains(Vector3D {3.1, 2.0, 3.0}));
    CHECK(sphere.Intersects(Sphere3D {Vector3D {4.0, 2.0, 3.0}, 1.0}));
    CHECK_FALSE(sphere.Intersects(Sphere3D {Vector3D {4.1, 2.0, 3.0}, 1.0}));

    const auto hit = Intersect(Ray3D {Vector3D {-4.0, 2.0, 3.0}, Vector3D {2.0, 0.0, 0.0}}, sphere);
    REQUIRE(hit.has_value());
    CHECK(*hit == Catch::Approx(1.5));
    CHECK_FALSE(Intersect(Ray3D {Vector3D {-4.0, 8.0, 3.0}, Vector3D {1.0, 0.0, 0.0}}, sphere).has_value());
}

TEST_CASE("Math frusta classify points, spheres, and boxes", "[Math][Geometry][Frustum]")
{
    const Matrix4D projection = Perspective(
            std::numbers::pi / 2.0,
            1.0,
            1.0,
            10.0,
            Handedness::Right,
            ClipDepthRange::ZeroToOne);
    const auto frustum = TryExtractFrustum(projection, ClipDepthRange::ZeroToOne);
    REQUIRE(frustum.has_value());

    CHECK(frustum->Contains(Vector3D {0.0, 0.0, -5.0}));
    CHECK_FALSE(frustum->Contains(Vector3D {0.0, 0.0, 1.0}));
    CHECK(frustum->Intersects(Sphere3D {Vector3D {0.0, 0.0, -9.5}, 1.0}));
    CHECK_FALSE(frustum->Intersects(Sphere3D {Vector3D {20.0, 0.0, -5.0}, 1.0}));
    CHECK(frustum->Intersects(AABB3D {Vector3D {-1.0, -1.0, -6.0}, Vector3D {1.0, 1.0, -4.0}}));
    CHECK_FALSE(frustum->Intersects(AABB3D {Vector3D {20.0, 20.0, -6.0}, Vector3D {21.0, 21.0, -5.0}}));
}

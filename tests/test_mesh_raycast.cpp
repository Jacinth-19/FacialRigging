#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/mesh.h"
#include "core/obj_io.h"
#include "core/raycast.h"
#include "core/camera.h"
using namespace fr;
using Catch::Approx;

TEST_CASE("procedural head is a closed, well-formed mesh") {
    Mesh m = makeProceduralHead(16, 24);
    REQUIRE(m.vertexCount() == 17u * 25u);
    REQUIRE(m.indices.size() % 3 == 0);
    for (auto i : m.indices) REQUIRE(i < m.vertexCount());
    REQUIRE(m.normals.size() == m.vertexCount());
    glm::vec3 lo = m.boundsMin(), hi = m.boundsMax();
    REQUIRE(hi.y - lo.y > hi.x - lo.x); // face taller than wide
}

TEST_CASE("OBJ parse handles polygons, negative indices and v/vt/vn") {
    const char* obj = "o quad\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1 4/4/1\nf -4/-4/-1 -2/-2/-1 -1/-1/-1\n";
    Mesh m; std::string err;
    REQUIRE(parseObj(obj, m, &err));
    CHECK(m.name == "quad");
    CHECK(m.vertexCount() == 4);
    CHECK(m.triangleCount() == 3); // quad fan (2) + tri (1)
    CHECK(m.uvs.size() == 4);
    CHECK(m.normals[0].z == Approx(1.0f));
}

TEST_CASE("OBJ round trip") {
    Mesh m = makeProceduralHead(8, 12);
    std::string path = "/tmp/fr_roundtrip.obj", err;
    REQUIRE(saveObj(path, m, &err));
    Mesh back;
    REQUIRE(loadObj(path, back, &err));
    REQUIRE(back.triangleCount() == m.triangleCount());
}

TEST_CASE("ray/triangle and ray/mesh intersection") {
    Ray r{{0.1f, 0.1f, 5.0f}, {0, 0, -1}};
    glm::vec3 bary;
    auto t = intersectTriangle(r, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, &bary);
    REQUIRE(t.has_value());
    CHECK(*t == Approx(5.0f));
    CHECK(bary.x + bary.y + bary.z == Approx(1.0f));
    CHECK_FALSE(intersectTriangle(Ray{{2, 2, 5}, {0, 0, -1}}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}).has_value());
    // back-face culling: ray from behind should miss when culled, hit when not
    Ray back{{0.1f, 0.1f, -5.0f}, {0, 0, 1}};
    CHECK_FALSE(intersectTriangle(back, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, nullptr, true).has_value());
    CHECK(intersectTriangle(back, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, nullptr, false).has_value());

    Mesh head = makeProceduralHead();
    auto hit = raycastMesh(Ray{{0, 0, 3}, {0, 0, -1}}, head);
    REQUIRE(hit.has_value());
    CHECK(hit->point.z > 0.3f);             // hits the front of the face first
    CHECK(glm::length(hit->normal) == Approx(1.0f));
}

TEST_CASE("screen point to ray through camera hits the model centre") {
    OrbitCamera cam; cam.distance = 2.0f;
    glm::mat4 V = cam.view(), P = cam.projection(16.0f / 9.0f);
    Ray r = screenPointToRay(640, 360, 1280, 720, V, P);
    CHECK(glm::length(r.origin - cam.position()) < 0.05f);
    CHECK(r.dir.z == Approx(-1.0f).margin(1e-4));
    Mesh head = makeProceduralHead();
    CHECK(raycastMesh(r, head).has_value());
}

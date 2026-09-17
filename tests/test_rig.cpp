#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "rig/rig.h"
#include "rig/rbf_deformer.h"
#include <glm/gtc/quaternion.hpp>
using namespace fr;
using Catch::Approx;

static Rig makeRig() { Rig r; r.setMesh(makeProceduralHead(24, 32)); r.buildDefaultFaceRig(); return r; }

TEST_CASE("bind pose evaluates to the original mesh") {
    Rig r = makeRig();
    auto v = r.evaluate();
    REQUIRE(v.size() == r.mesh.vertexCount());
    for (size_t i = 0; i < v.size(); ++i) REQUIRE(glm::length(v[i] - r.mesh.positions[i]) < 1e-5f);
}

TEST_CASE("skin weights are normalised and only reference existing bones") {
    Rig r = makeRig();
    REQUIRE(r.hasSkin());
    for (auto& inf : r.skin) {
        float s = inf.weights.x + inf.weights.y + inf.weights.z + inf.weights.w;
        CHECK(s == Approx(1.0f).margin(1e-4));
        for (int k = 0; k < 4; ++k) CHECK(inf.bones[k] < int(r.skeleton.bones.size()));
    }
}

TEST_CASE("jaw rotation moves the chin down and leaves the forehead") {
    Rig r = makeRig();
    int jaw = r.skeleton.find("Jaw"); REQUIRE(jaw >= 0);
    r.skeleton.bones[jaw].poseRotation = glm::angleAxis(glm::radians(15.0f), glm::vec3(1, 0, 0));
    auto v = r.evaluate();
    glm::vec3 lo = r.mesh.boundsMin(), hi = r.mesh.boundsMax();
    float chinBefore = 1e9f, chinAfter = 1e9f; glm::vec3 browBefore(0.0f), browAfter(0.0f); float bestBrow = 1e9f;
    for (size_t i = 0; i < v.size(); ++i) {
        const glm::vec3& p = r.mesh.positions[i];
        if (p.z > 0.2f && p.y < chinBefore) { chinBefore = p.y; chinAfter = v[i].y; }
        float d = glm::length(p - glm::vec3(0, lo.y + 0.8f * (hi.y - lo.y), hi.z));
        if (d < bestBrow) { bestBrow = d; browBefore = p; browAfter = v[i]; }
    }
    CHECK(chinAfter < chinBefore - 0.01f);
    CHECK(glm::length(browAfter - browBefore) < 1e-4f);
}

TEST_CASE("blendshape weights scale deltas linearly and control points drive them") {
    Rig r = makeRig();
    int smile = r.findBlendShape(shapes::MouthSmile); REQUIRE(smile >= 0);
    REQUIRE_FALSE(r.blendShapes[smile].indices.empty());
    r.setBlendWeight(smile, 0.5f);
    auto half = r.evaluate();
    r.setBlendWeight(smile, 1.0f);
    auto full = r.evaluate();
    uint32_t vi = r.blendShapes[smile].indices[0];
    glm::vec3 dHalf = half[vi] - r.mesh.positions[vi], dFull = full[vi] - r.mesh.positions[vi];
    CHECK(glm::length(dFull - 2.0f * dHalf) < 1e-5f);

    // control point bound to MouthSmile: dragging along its axis sets the weight
    r.resetPose();
    int cp = -1; for (size_t i = 0; i < r.controlPoints.size(); ++i) if (r.controlPoints[i].name == "MouthCornerR") cp = int(i);
    REQUIRE(cp >= 0);
    const auto& c = r.controlPoints[cp];
    r.moveControlPoint(cp, c.driveAxis * c.driveRange * 0.75f);
    CHECK(r.blendWeight(shapes::MouthSmile) == Approx(0.75f).margin(1e-4));
    r.moveControlPoint(cp, c.driveAxis * c.driveRange * 5.0f);
    CHECK(r.blendWeight(shapes::MouthSmile) == Approx(1.0f)); // clamped
    r.syncControlPointsFromRig();
    CHECK(glm::length(r.controlPoints[cp].offset - c.driveAxis * c.driveRange) < 1e-5f);
}

TEST_CASE("RBF deformer interpolates handle displacements and decays to zero") {
    RbfDeformer rbf;
    rbf.setHandles({{{0, 0, 0}, {0, 0.1f, 0}, 0.5f}, {{1, 0, 0}, {0, -0.1f, 0}, 0.5f}});
    CHECK(glm::length(rbf.displacementAt({0, 0, 0}) - glm::vec3(0, 0.1f, 0)) < 1e-4f);
    CHECK(glm::length(rbf.displacementAt({1, 0, 0}) - glm::vec3(0, -0.1f, 0)) < 1e-4f);
    CHECK(glm::length(rbf.displacementAt({5, 0, 0})) == Approx(0.0f));
    CHECK(RbfDeformer::kernel(0.0f, 1.0f) == Approx(1.0f));
    CHECK(RbfDeformer::kernel(1.0f, 1.0f) == Approx(0.0f));
}

TEST_CASE("free-form control point warps nearby vertices only") {
    Rig r = makeRig();
    glm::vec3 hi = r.mesh.boundsMax();
    int cp = r.addControlPoint(glm::vec3(0, 0, hi.z), "Nose");
    r.bindFreeForm(cp, 0.2f);
    r.moveControlPoint(cp, glm::vec3(0, 0, 0.05f));
    auto v = r.evaluate();
    uint32_t nv = r.controlPoints[cp].nearestVertex;
    CHECK(v[nv].z > r.mesh.positions[nv].z + 0.03f);
    // back of head untouched
    for (size_t i = 0; i < v.size(); ++i) if (r.mesh.positions[i].z < -0.2f) REQUIRE(glm::length(v[i] - r.mesh.positions[i]) < 1e-6f);
}

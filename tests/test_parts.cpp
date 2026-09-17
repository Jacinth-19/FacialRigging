#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/obj_io.h"
#include "rig/rig.h"
#include "rig/blendshape_io.h"
#include "app/pipeline.h"
#include <fstream>
using namespace fr;
using Catch::Approx;

TEST_CASE("OBJ groups become mesh parts and survive save/load") {
    const char* obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nv 2 2 2\ng Face\nf 1 2 3\nf 2 4 3\ng TeethLower\nf 1 2 5\n";
    { std::ofstream f("/tmp/fr_parts.obj"); f << obj; }
    Mesh m; std::string err; REQUIRE(loadObj("/tmp/fr_parts.obj", m, &err));
    REQUIRE(m.parts.size() == 2);
    CHECK(m.parts[0].name == "Face"); CHECK(m.parts[0].indexCount == 6);
    CHECK(m.parts[1].name == "TeethLower"); CHECK(m.parts[1].firstIndex == 6); CHECK(m.parts[1].indexCount == 3);
    CHECK(m.findPart("TeethLower") == 1); CHECK(m.findPart("nope") == -1);
    CHECK(m.partVertices(1).size() == 3);
    glm::vec3 lo, hi; m.partBounds(1, lo, hi); CHECK(hi.z == Approx(2.0f)); m.partBounds(0, lo, hi); CHECK(hi.z == Approx(0.0f));
    REQUIRE(m.sourceVertex.size() == m.vertexCount());
    REQUIRE(saveObj("/tmp/fr_parts2.obj", m, &err));
    Mesh m2; REQUIRE(loadObj("/tmp/fr_parts2.obj", m2, &err));
    REQUIRE(m2.parts.size() == 2); CHECK(m2.parts[1].name == "TeethLower");
}

TEST_CASE("FRBS blendshapes round-trip and remap onto seam-split vertices") {
    std::vector<BlendShape> shapes(1); shapes[0].name = "jawOpen"; shapes[0].indices = {2}; shapes[0].deltas = {glm::vec3(0, -1, 0)};
    REQUIRE(saveBlendShapesFRBS("/tmp/fr_unit.fbs", shapes, 3));
    std::vector<BlendShape> in; std::string err;
    CHECK_FALSE(loadBlendShapesFRBS("/tmp/fr_unit.fbs", in, 99, &err)); // vertex-count guard
    REQUIRE(loadBlendShapesFRBS("/tmp/fr_unit.fbs", in, 3, &err));
    REQUIRE(in.size() == 1); CHECK(in[0].name == "jawOpen"); CHECK(in[0].indices[0] == 2);
    Mesh m; m.positions.resize(5); m.sourceVertex = {0, 1, 2, 2, 1}; // source vertex 2 was split into 2 and 3
    remapBlendShapesToMesh(in, m);
    REQUIRE(in[0].indices.size() == 2); CHECK(in[0].indices[0] == 2); CHECK(in[0].indices[1] == 3);
    CHECK(canonicalShapeName("jawOpen") == std::string(shapes::JawOpen));
    CHECK(canonicalShapeName("mouthSmile_L") == std::string(shapes::MouthSmile));
    CHECK(canonicalShapeName("tongueOut").empty());
}

TEST_CASE("ICT-FaceKit head loads with separated parts and 53 authored blendshapes") {
    Pipeline p; std::string err;
    REQUIRE(p.loadModel(std::string(FR_ASSET_DIR) + "/models/ict_face/ict_face.obj", &err));
    const Mesh& m = p.rig.mesh;
    for (const char* part : {"Face", "EyebrowL", "EyebrowR", "EyeL", "EyeR", "TeethUpper", "TeethLower", "GumsUpper", "GumsLower", "Tongue", "Eyelashes"})
        CHECK(m.findPart(part) >= 0);
    REQUIRE(p.authoredShapes().size() == 53);
    p.buildDefaultRig();
    CHECK(p.authoredCanonicalCoverage == int(std::size(shapes::All)));
    CHECK(p.rig.blendShapes.size() == std::size(shapes::All) + 53);
    // lower teeth are rigidly bound to the jaw bone; upper teeth to the head
    auto parts = p.rig.detectParts();
    REQUIRE(parts.teethLower >= 0); REQUIRE(parts.teethUpper >= 0);
    for (uint32_t v : m.partVertices(parts.teethLower)) { CHECK(p.rig.skin[v].bones[0] == 1); CHECK(p.rig.skin[v].weights[0] == Approx(1.0f)); }
    for (uint32_t v : m.partVertices(parts.teethUpper)) { CHECK(p.rig.skin[v].bones[0] == 0); }
    // authored jawOpen actually lowers the chin
    int jaw = p.rig.findBlendShape(shapes::JawOpen); REQUIRE(jaw >= 0);
    glm::vec3 lo, hi; m.partBounds(parts.face, lo, hi);
    float minDy = 0; for (auto& d : p.rig.blendShapes[size_t(jaw)].deltas) minDy = std::min(minDy, d.y);
    CHECK(minDy < -0.05f * (hi.y - lo.y));
}

TEST_CASE("Force Symmetry mirrors control-point offsets onto the L/R partner") {
    Rig rig; rig.setMesh(makeProceduralHead()); rig.buildDefaultFaceRig();
    int l = -1, r = -1;
    for (size_t i = 0; i < rig.controlPoints.size(); ++i) { if (rig.controlPoints[i].name == "BrowL") l = int(i); if (rig.controlPoints[i].name == "BrowR") r = int(i); }
    REQUIRE(l >= 0); REQUIRE(r >= 0);
    CHECK(rig.mirrorPartner(l) == r); CHECK(rig.mirrorPartner(r) == l);
    rig.forceSymmetry = false; rig.moveControlPoint(l, glm::vec3(0.01f, 0.02f, 0.0f));
    CHECK(rig.controlPoints[size_t(r)].offset == glm::vec3(0.0f));
    rig.forceSymmetry = true; rig.moveControlPoint(l, glm::vec3(0.01f, 0.03f, 0.0f));
    CHECK(rig.controlPoints[size_t(r)].offset.x == Approx(-0.01f)); CHECK(rig.controlPoints[size_t(r)].offset.y == Approx(0.03f));
    int chin = -1; for (size_t i = 0; i < rig.controlPoints.size(); ++i) if (rig.controlPoints[i].name == "Chin") chin = int(i);
    REQUIRE(chin >= 0); CHECK(rig.mirrorPartner(chin) == -1); // centre-line point has no partner
}

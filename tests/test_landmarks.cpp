#include <catch2/catch_test_macros.hpp>
#include "app/pipeline.h"
#include "rig/landmarks.h"
using namespace fr;

TEST_CASE("front render covers the face and lifts pixels back to the surface", "[landmarks]") {
    Mesh m = makeProceduralHead(24, 32);
    FrontRender r = renderFront(m, 128);
    size_t hits = 0; for (auto& w : r.world) hits += w.w > 0.5f;
    REQUIRE(hits > 128 * 128 / 6);
    // the centre pixel is on the surface near the front (max z)
    const glm::vec4& c = r.world[64 * 128 + 64];
    REQUIRE(c.w > 0.5f);
    REQUIRE(c.z > m.boundsMax().z - 0.2f * (m.boundsMax().z - m.boundsMin().z));
    glm::vec2 px = r.pixelOf(glm::vec3(c));
    REQUIRE(std::abs(px.x - 64.5f) < 1.0f); REQUIRE(std::abs(px.y - 64.5f) < 1.0f);
}

TEST_CASE("proportional landmarks are laid out plausibly", "[landmarks]") {
    Mesh m = makeProceduralHead(16, 24);
    FaceLandmarks lm = proportionalLandmarks(m);
    REQUIRE(lm.mouth.y < lm.eyeL.y); REQUIRE(lm.eyeL.y < lm.browL.y); REQUIRE(lm.cornerL.x < lm.cornerR.x);
    symmetrize(lm, 0.0f);
    REQUIRE(lm.cornerL.x == -lm.cornerR.x);
}

TEST_CASE("dlib auto-landmarks place the ICT rig landmarks on the real features", "[landmarks][ict]") {
    std::string why;
    if (!landmarkerAvailable(&why)) { WARN("skipping: " << why); return; }
    Pipeline p; std::string err;
    REQUIRE(p.loadModel(std::string(FR_ASSET_DIR) + "/models/ict_face/ict_face.obj", &err));
    p.autoLandmarks = Pipeline::AutoLandmarks::On;
    p.buildDefaultRig();
    REQUIRE(p.lastLandmarks.found);
    const FaceLandmarks& L = p.rig.landmarks;
    REQUIRE(L.points68.size() == 68);
    // eyelid landmarks lie over the eyeball parts (known bounds: EyeL x 0.047..0.136, y 0.132..0.224)
    int eyeL = p.rig.mesh.findPart("EyeL"), eyeR = p.rig.mesh.findPart("EyeR"); REQUIRE(eyeL >= 0);
    glm::vec3 lo, hi; p.rig.mesh.partBounds(eyeL, lo, hi);
    glm::vec3 lo2, hi2; p.rig.mesh.partBounds(eyeR, lo2, hi2);
    // our "L" is -x; whichever eyeball part is at -x must contain the L eyelid point in x/y (with a lid margin)
    glm::vec3 lLo = lo.x < lo2.x ? lo : lo2, lHi = lo.x < lo2.x ? hi : hi2;
    REQUIRE(L.eyeL.x > lLo.x - 0.01f); REQUIRE(L.eyeL.x < lHi.x + 0.01f);
    REQUIRE(L.eyeL.y > lLo.y - 0.02f); REQUIRE(L.eyeL.y < lHi.y + 0.03f);
    // mouth between the teeth rows in y (lower teeth y -0.105..-0.017)
    int teeth = p.rig.mesh.findPart("TeethLower"); REQUIRE(teeth >= 0);
    p.rig.mesh.partBounds(teeth, lo, hi);
    REQUIRE(L.mouth.y > lo.y - 0.03f); REQUIRE(L.mouth.y < hi.y + 0.05f);
    REQUIRE(L.cornerL.x < L.mouth.x); REQUIRE(L.cornerR.x > L.mouth.x);
    // brows above the eyes, chin below the mouth, and the proportional guess is measurably different
    REQUIRE(L.browL.y > L.eyeL.y); REQUIRE(L.chin.y < L.mouth.y);
    FaceLandmarks guess = proportionalLandmarks(p.rig.mesh);
    REQUIRE(glm::length(guess.mouth - L.mouth) > 0.005f);
    // control points followed the detection
    bool foundCorner = false;
    for (auto& cp : p.rig.controlPoints) if (cp.name == "MouthCornerL") { foundCorner = true; REQUIRE(glm::length(cp.restPosition - L.cornerL) < 0.02f); }
    REQUIRE(foundCorner);
}
